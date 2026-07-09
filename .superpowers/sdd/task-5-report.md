# Task 5 Report — M2: EEST state fixture 对照 harness

## 状态：DONE

## 提交
commit `cb95c09f7da010bd7bd35798f8739d0fc8b03b67`
```
test(bcos-evm-ref): EEST state fixture harness (stateRoot + logsHash, Cancun+, pinned EEST)

 bcos-evm-ref/test/CMakeLists.txt        |   1 +
 bcos-evm-ref/test/EEST_VERSION          |   1 +
 bcos-evm-ref/test/eth/EestStateTest.cpp | 136 ++++++++++++++++++++++++++++++++
 3 files changed, 138 insertions(+)
```
（第一次 `git commit` 触发了仓库 pre-commit clang-format hook，就地重排了 include 顺序并重新换行了一处函数调用；重跑 build + 全量 fixtures 复核无误后再次 `git add` + `git commit` 生成上面这个提交。）

## Step 1: EEST 版本 pin

`bcos-evm-ref/test/EEST_VERSION` 内容：
```
eest-release: v5.4.0 (fixtures_develop.tar.gz, sha256 3e2b02d49fe903eda4fd8caca5cbf0d139c470e97e1de9a85299b1b034f97099, 与 bcos-evm/test/eth-eest-test assets/upstream-pins.json 配对)
```
与仓库既有 `bcos-evm/test/eth-eest-test/assets/upstream-pins.json` 中 `eest.release=v5.4.0`、`eest.sha256` 逐字核对一致（64 hex 字符）。未下载 fixtures：本地已有
`build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src`（CMake FetchContent 产物）。

### fixtures 目录布局确认
`evm_ref_eest_fixtures-src` 根目录直接含 `state_tests/`（不是 `fixtures/state_tests/`），另有
`blockchain_tests`、`blockchain_tests_sync`、`blockchain_tests_engine`、`blockchain_tests_engine_x`、
`transaction_tests`、`.meta`。`state_tests/` 下按 fork 名分子目录：
`berlin/ byzantium/ cancun/ constantinople/ frontier/ homestead/ istanbul/ london/ osaka/ paris/ prague/ shanghai/ static/`，
共 2723 个 `*.json`（`cancun/` 60、`prague/` 105、`osaka/` 53，其余为 pre-Cancun fork）。
`EestStateTest.cpp` 里 `stateTestsDir()` 的双路径兼容逻辑（先探测 `<root>/state_tests`，否则退回
`<root>/fixtures/state_tests`）在本仓库场景下命中前者。

## Step 2/3: 落盘代码 + CMake

- 新建 `bcos-evm-ref/test/eth/EestStateTest.cpp`：逐字落盘 brief 给出的实现（controller 覆盖的 Step 1 pin
  决策未改变代码本体语义）。`git commit` 时 pre-commit hook 对 include 顺序 / 一行长函数调用做了
  clang-format 重排（语义等价），已重新构建 + 跑全量 fixtures 验证后再提交。
- `bcos-evm-ref/test/CMakeLists.txt`：`add_executable(bcos-evm-ref-eth-tests ...)` 源列表追加
  `eth/EestStateTest.cpp`；链接目标仍为 `bcosevmref::eth` + `GTest::gtest` + `GTest::gtest_main`（未变）。
- `cmake --build bcos-evm-ref/build`：编译链接通过，无警告（除既有的
  `ld: warning: ignoring duplicate libraries: libgtest.a`，Task 3/4 已存在，与本次改动无关）。

## Step 4: 无 fixtures 时 SKIP 行为

`unset EVM_REF_EEST_ROOT` 后运行 `ctest --test-dir bcos-evm-ref/build --output-on-failure` 与直接跑二进制：
```
[  PASSED  ] 9 tests.
[  SKIPPED ] 1 test, listed below:
[  SKIPPED ] EestState.Fixtures
```
（`EestState.Fixtures` 报 `EVM_REF_EEST_ROOT not set` 后 `GTEST_SKIP`，其余 9 个既有用例全绿——与基线
9/9 一致，新增 harness 不影响既有用例。）

## Step 5: 冒烟

按控制器指令，先用小子集验证判据，再尝试全量（20 分钟超时观察）。

### 5a. 小子集（3 个 Cancun 子目录，20 files）
从 `state_tests/cancun/` 拷贝 `eip5656_mcopy`(6)、`eip6780_selfdestruct`(11)、`eip7516_blobgasfee`(3) 共
20 个 fixture 到临时目录 `<scratch>/eest-smoke/state_tests/cancun/...`，`EVM_REF_EEST_ROOT` 指向该临时根：
```
EEST state: files=20 skipped=0 failed_files=0
[       OK ] EestState.Fixtures (66 ms)
```

### 5b. 全量（`EVM_REF_EEST_ROOT=$PWD/build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src`）
小子集验证判据无误后尝试全量运行，未触发 20 分钟超时（远低于预期），故未降级为子集专项：
```
EEST state: files=2723 skipped=0 failed_files=0
[       OK ] EestState.Fixtures (16895 ms)   # clang-format 前一次跑 19368 ms，语义等价，两次均全绿
```
`ctest --test-dir bcos-evm-ref/build -R BcosEvmRefEthTests --output-on-failure`：
```
1/1 Test #1: BcosEvmRefEthTests ...............   Passed   19.16 sec
100% tests passed, 0 tests failed out of 1
```
- files = 2723（含全部 fork 子目录；harness 内部按 `rev >= EVMC_CANCUN` 过滤 case，非 Cancun+ 的旧 fork
  文件被遍历计数但不产生断言，不影响 `failed_files` 计数口径）
- skipped = 0（未设置 `EVM_REF_EEST_SKIP`）
- failed_files = 0
- 耗时：约 17–19 秒（两次测得 16895 ms / 19368 ms，机器噪声范围内），远低于 20 分钟超时预算

### 失败样本
无。全量 2723 个 fixture 文件、覆盖 Cancun（60）/ Prague（105）/ Osaka（53）共 218 个含 Cancun+ case 的
文件，stateRoot + logsHash 判据逐位通过，0 failed_files。`bcos-evm-ref` 是 evmone 的薄封装
（`runTransaction`/`runBlockFinalize`/`applyStateDiff` 均直接转发到 evmone 自身实现），全绿符合预期——
不是自研执行器，没有独立语义分歧的空间；这与仓库另一个基于自研执行器对照的项目
（`bcos-evm/test/eth-eest-test/reports/` 下大量历史失败报告）分属不同项目，互不冲突，不构成矛盾证据。

## 备注

- 未改动判据、未引入 skip 清单文件（`EVM_REF_EEST_SKIP` 机制已实现但本次未使用，0 skip）。
- `git show --stat HEAD` 确认本次提交仅含 3 个文件（`CMakeLists.txt` 1 行改动 + 2 个新文件），无副作用改动。
- 冒烟用临时小子集目录位于 scratchpad
  (`/private/tmp/claude-502/-Users-octopus-octo-code/5510a170-6f1d-4aad-82f5-b58a66dc6737/scratchpad/eest-smoke`)，
  未入库，仅用于验证判据管线，不影响本次提交内容。
- 本文件此前内容为另一个不相关任务编号方案（"Task 5: H6 — Unsupported formats → GTEST_SKIP"，属
  `bcos-evm/test/eth-eest-test` 项目）的残留报告，已按控制器指示的路径整篇覆盖为本任务（bcos-evm-ref
  M1+M2 plan Task 5）报告。

---
## 控制器更正（Task 5 审查者实测，2026-07-09）
本报告上文"覆盖 Cancun(60)/Prague(105)/Osaka(53) 共 218 个含 Cancun+ case 的文件"一句**有误**：
static/ 目录（2446 文件，占 90%）为旧版 GST 迁移产物，每文件含数百个具名子测试且多数覆盖 Cancun/Prague/Osaka。
实际覆盖 = **2717/2723 个文件含 Cancun+ case，Cancun+ case 总数 55,233，全部通过**（63,556 全 fork case 中）。
Task 6 验收记录以此数字为准。
