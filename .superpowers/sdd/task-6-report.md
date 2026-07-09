# Task 6 报告：M2 收口 — 全量 EEST 运行记录 + 根构建挂接 + README 验收记录

（本文件此前内容为另一个不相关任务编号方案"Task 6: H7 — Failure bucket reports (JSON + MD)"
的残留报告，属 `bcos-evm/test/eth-eest-test` 项目，与本任务无关。已按控制器指示的路径
整篇覆盖为本任务——bcos-evm-ref M1+M2 plan Task 6——报告，与 Task 5 报告处理方式一致。）

## 范围与前提（控制器决定，覆盖 brief 对应步骤）

- Step 3（全量 EEST state 运行）已由 Task 5 完成，并经审查者独立复测更正数字（详见
  `.superpowers/sdd/task-5-report.md` 末尾"控制器更正"节）。本任务**未重跑**该全量测试
  （控制器允许重跑但非必须；且不重跑不影响验收数字，数字以审查者复测为准）。
- Step 1/2（根 CMakeLists 挂接 + configure 验证）按 brief 执行，且按控制器指示对 Step 2
  做了一次轻量化尝试（见下）。

## Step 1: 根 `CMakeLists.txt` 挂接

在 `add_subdirectory(bcos-evm)`（第 67 行，位于 `if(FULLNODE)` 块内）之后插入 option 块：

```diff
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -65,6 +65,10 @@ if(FULLNODE)
     find_package(blst CONFIG REQUIRED)
 
     add_subdirectory(bcos-evm)
+    option(BCOS_EVM_REF "Build bcos-evm-ref reference EVM module" OFF)
+    if(BCOS_EVM_REF)
+        add_subdirectory(bcos-evm-ref)
+    endif()
     add_subdirectory(bcos-sealer)
     add_subdirectory(bcos-security)
     add_subdirectory(bcos-scheduler)
```

与 brief 给出的代码块逐字一致，缩进沿用周边 `if(FULLNODE)` 块内 4 空格风格。默认 `OFF`，
关闭时对现有构建路径零影响（`add_subdirectory(bcos-evm-ref)` 处于 `if(BCOS_EVM_REF)` 内，
不会被执行）。

## Step 2: configure 验证（未降级，完整验证通过）

**未触发 brief 描述的"坑"**：本仓库已存在一个此前配置过的根构建目录 `build/`
（`FULLNODE=ON`，`CMAKE_HOME_DIRECTORY` 指向本仓库根，`vcpkg_installed` 已装满 2.4G
依赖，日期 2026-07-07），且全局 vcpkg 二进制缓存 `~/.cache/vcpkg/archives` 已有 3.6G
归档。据此判断：全新 `cmake -B build-refcheck` configure 会命中 vcpkg 二进制缓存直接
安装（而非从源码重新编译），不会触发"全新装 30+ 分钟依赖"的场景，因此按 brief 原始
命令执行、后台监控进度，未提前降级为语法级验证。

命令与结果：

```bash
cmake -B build-refcheck -S . \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DBCOS_EVM_REF=ON
```

- 后台执行，全程监控日志，**26.5 秒**内完成 configure（远低于"10 分钟无进展"的中止
  阈值），未见任何依赖重新编译/下载的迹象（vcpkg 直接命中已安装/缓存的包）。
- 日志末尾：
  ```
  -- Configuring done (26.5s)
  -- Generating done (0.4s)
  -- Build files have been written to: .../build-refcheck
  ```
- `grep -i "error"` 对 configure 全量日志（1061 行）无匹配。
- 缓存验证：`build-refcheck/CMakeCache.txt` 含 `BCOS_EVM_REF:BOOL=ON`、
  `BCOS_EVM_REF_TESTS:BOOL=OFF`（非 standalone 场景下该选项默认 OFF，符合
  `bcos-evm-ref/CMakeLists.txt` 中
  `option(BCOS_EVM_REF_TESTS ... ${BCOS_EVM_REF_STANDALONE})` 的设计）。
- 子目录确实被挂接：`build-refcheck/bcos-evm-ref/CMakeFiles/bcos-evm-ref-eth.dir` 已
  生成（证明 `add_subdirectory(bcos-evm-ref)` 被执行，target `bcos-evm-ref-eth` 参与
  根构建的 CMake 图）。
- 验证完成后已删除 `build-refcheck`（`rm -rf build-refcheck`，`ls` 确认不存在）。

**结论**：configure 完整验证通过，无需降级为语法级检查；`BCOS_EVM_REF` 默认 `OFF`
对现有构建零回归（该判据在完整 configure 通过的前提下已经过验证，不仅是 grep 静态
确认）。

## Step 3: 全量 EEST 运行（沿用 Task 5 + 审查者复测数字，未重跑）

见控制器决定。验收数字：

- EEST release: `v5.4.0`（`fixtures_develop.tar.gz`），见
  `bcos-evm-ref/test/EEST_VERSION`，与 evmone REF `3585c2cb` 配对（Task 5 Step 1
  选定）。
- harness 日志原样：`EEST state: files=2723 skipped=0 failed_files=0`
  （`EVM_REF_EEST_ROOT=$PWD/build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src`，耗时
  约 17–19 秒，两次测得 16895 ms / 19368 ms）。
- 全 fork case 总数 63,556；其中 **2717/2723 个文件含 Cancun+ case**，
  **Cancun+ case 总数 55,233，全部通过**（Task 5 报告"218 个文件"的原始表述有误，已由
  审查者复测更正——`static/`（2446 文件，占 90%）为旧版 GST 迁移产物，每文件含数百个
  具名子测试，多数覆盖 Cancun/Prague/Osaka，原表述遗漏了这部分）。
- skip 清单（`EVM_REF_EEST_SKIP`）：无，本次运行未设置该变量，0 skip。
- 分诊二进制一段（brief Step 3 描述的 evmone-statetest 自建流程）因 0 failed_files，
  未执行（无失败样本需要交叉验证）。

## Step 4: `bcos-evm-ref/README.md`

新建文件，全文：

```markdown
# bcos-evm-ref

Spec: `bcos-evm/docs/superpowers/specs/2026-07-08-bcos-evm-ref-evmone-reuse-design.md` (rev.3)

复用 evmone::state（vcpkg overlay port，REF 3585c2cb）的标准 ETH/OpStack 参考模块。
与现有 bcos-evm/ 严格隔离（互不 include）。

## Build (standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build

## Build (in-tree, as part of FISCO-BCOS root build)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DBCOS_EVM_REF=ON
    cmake --build build --target bcos-evm-ref-eth

`BCOS_EVM_REF`（根 `CMakeLists.txt`，默认 `OFF`）挂接本模块的 `add_subdirectory`；关闭时对现有构建零影响。

## Test

    export EVM_REF_EEST_ROOT=<EEST fixtures root>   # 未设置则 EEST 用例 SKIP
    ctest --test-dir build --output-on-failure

## M2 验收记录

- EEST release: 见 `test/EEST_VERSION`（v5.4.0, `fixtures_develop.tar.gz`；与 evmone REF 3585c2cb 配对；Task 5 Step 1 选定）
- state fixtures（harness 日志原样）：`files=2723 skipped=0 failed_files=0`
  （耗时约 17–19s；`ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests` 全绿）
  - 2723 个 fixture 文件覆盖全部 fork 子目录；其中 **2717 个文件含 Cancun+ case**
    （harness 内部按 `rev >= EVMC_CANCUN` 过滤，非 Cancun+ 的旧 fork 文件被遍历计数但不产生断言）
  - **Cancun+ case 总数 55,233，全部通过**（63,556 全 fork case 中）
- skip 清单（`EVM_REF_EEST_SKIP`）：无（本次运行未设置该变量，0 skip）
- 数字来源：Task 5 审查者独立复测（详见 `.superpowers/sdd/task-5-report.md` 控制器更正节）
```

（README 中相对路径 `../vcpkg/...` 沿用 brief 模板；实际仓库布局中 `vcpkg/` 是仓库根
目录，`bcos-evm-ref/` 是其子目录，standalone 模式下从 `bcos-evm-ref/` 内执行该命令时
相对路径正确。in-tree 命令段为本报告补充，说明 `BCOS_EVM_REF` 挂接后的实际用法，公开
头文件路径 `include/bcos-evm-ref/` 不影响 README 内容表述。）

## Step 5: Commit

```bash
git add CMakeLists.txt bcos-evm-ref/README.md
git commit -m "feat(bcos-evm-ref): wire into root build (BCOS_EVM_REF option) and record M2 EEST results"
```

提交结果：

```
[feat-evm-opstack-on-evmone e717e6496] feat(bcos-evm-ref): wire into root build (BCOS_EVM_REF option) and record M2 EEST results
 2 files changed, 38 insertions(+)
 create mode 100644 bcos-evm-ref/README.md
```

`git show --stat HEAD`：

```
commit e717e649632d3b78170db6c0ceed3f1afa2c8515
Author: octopus <912554887@qq.com>
Date:   Thu Jul 9 10:32:28 2026 +0800

    feat(bcos-evm-ref): wire into root build (BCOS_EVM_REF option) and record M2 EEST results

 CMakeLists.txt         |  4 ++++
 bcos-evm-ref/README.md | 34 ++++++++++++++++++++++++++++++++++
 2 files changed, 38 insertions(+)
```

提交仅含这 2 个文件（`git status --short` 确认提交前后工作区其余改动——如
`.superpowers/sdd/task-{2,3,4,5}-report.md` 的修改、大量 worktree 外无关未跟踪目录——
均未被本次提交纳入）。pre-commit hook 对 `bcos-evm-ref/test/eth/EestStateTest.cpp`
（Task 5 产物，已提交过）执行了一次 `clang-format -i`，因该文件已符合格式规范，未产生
实际 diff（提交后 `git status --short bcos-evm-ref/` 为空），无副作用。

## 备注 / concerns

- Step 2 未按 brief 预案降级为语法级验证——因为完整 configure 在 26.5 秒内即成功通过，
  比降级判据更强的证据（实际 CMake 图生成，而非仅静态 grep）。已确认后清理了
  `build-refcheck` 目录。
- Step 3 沿用 Task 5 + 审查者复测数字，未在本任务中重新运行 ctest；如需独立三次复测可
  额外执行 `EVM_REF_EEST_ROOT=$PWD/build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src
  ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests --output-on-failure`
  （约 18 秒）。
- M1+M2 plan 至此全部 6 个 Task 完成；后续 M3/M4+M5/M6 已在 brief 末尾列为下一轮 plan
  范围，本任务不涉及。
