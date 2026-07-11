# bcos-evm-ref 布局迁移（对齐仓库主流双名约定）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 把 `bcos-evm-ref` 的目录布局从「`include/` 公开头树 + 顶层域源目录」迁移为仓库主流的**双名布局**（`bcos-evm-ref/bcos-evm-ref/<域>/` 头源同居），与 bcos-crypto/bcos-utilities/bcos-framework/bcos-ledger 等一致。**全部 `#include <bcos-evm-ref/…>` 语句零改动**；单提交、纯 `git mv` + 构建配置调整。

**方案依据（用户已裁定方案 A，2026-07-11）**：外层目录作 include root，双名内层目录使 `<bcos-evm-ref/opstack/OpHost.h>` 原样命中。

## Global Constraints

- 迁移**不改任何源文件内容**（`git diff --stat` 中 .h/.cpp 只应出现 rename，无内容 diff）——`#include` 全部保持
- 迁移后必须全绿：opstack 85/85、eth 9 PASS/3 env-SKIP、`scripts/upstream-diff.sh` 8/8
- 用 `git mv` 保 rename 检测/历史
- 历史文档（台账/审计/spec 内的 `opstack/…:行号` 引用）**不改**——pin 快照
- 工作目录 `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref`；执行前工作树须干净

## 目标布局

```
bcos-evm-ref/
├── bcos-evm-ref/
│   ├── adapter/   StateDiffWriteback.h  StateViewAdapter.h          （2 头，header-only）
│   ├── eth/       EthTransition.h  EthTransition.cpp               （1+1）
│   └── opstack/   Op*.h + Op*.cpp + RollupCost.*                   （13 头 + 12 源并排）
├── test/  docs/  scripts/  spike/                                   （不动）
├── CMakeLists.txt  README.md  vcpkg.json  vcpkg-configuration.json  iwyu-bcos-evm-ref.imp
```

### Task 1: 布局迁移（单提交）

**Files:** 15 头 + 13 源 `git mv`；Modify: `CMakeLists.txt`、`scripts/upstream-diff/manifest.tsv`、`README.md:7`。**不动**：`test/`（无路径引用，已核）、`iwyu-bcos-evm-ref.imp`（映射全是 evmc/evmone 头，无本模块路径，已核）、`spike/`。

- [ ] **Step 1: 前置检查（防遮蔽风险——本 plan 唯一非机械点）**

include root 变为模块根后，`<test/…>` 前缀理论上会先命中本模块 `test/` 目录、遮蔽 evmone 导出的 `<test/state/…>`/`<test/utils/…>` 头。逐项确认无碰撞：

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref
test ! -d test/state && test ! -d test/utils && echo "无遮蔽风险" || echo "STOP: test/ 下存在 state|utils 子目录"
rtk git status --short . | head -3   # 须为空
```

Expected: `无遮蔽风险` + 工作树干净。若 STOP：终止并上报（不得自行改名 test 子目录）。

- [ ] **Step 2: git mv**

```bash
mkdir -p bcos-evm-ref
rtk git mv include/bcos-evm-ref/adapter bcos-evm-ref/adapter
rtk git mv include/bcos-evm-ref/eth     bcos-evm-ref/eth
rtk git mv include/bcos-evm-ref/opstack bcos-evm-ref/opstack
rtk git mv eth/EthTransition.cpp bcos-evm-ref/eth/EthTransition.cpp
for f in OpForkSchedule OpHost OpPrecompiles OpFeeParams OpPredeploys RollupCost OpValidate OpTransition OpExecCommon OpDepositTx OpReceiptMeta OpBlockFinalize; do
  rtk git mv "opstack/$f.cpp" "bcos-evm-ref/opstack/$f.cpp"
done
rmdir include/bcos-evm-ref include eth opstack   # 空目录清除（git 不跟踪空目录，rmdir 失败即有遗漏文件，停下检查）
```

- [ ] **Step 3: CMakeLists.txt 三处**

① eth 库：

```cmake
add_library(bcos-evm-ref-eth STATIC bcos-evm-ref/eth/EthTransition.cpp)
```

② 两个 `target_include_directories` 均改为（注释同步更新）：

```cmake
# PUBLIC include = 模块根（仓库主流双名布局），消费者以 <bcos-evm-ref/eth/EthTransition.h> 引用；
# 编译期隔离不变：bcos-evm 不在 include path，越界 #include 仍编译错。
# 注意：本 include root 下另有 test/ 等目录，evmone 的 <test/state/…> 头依赖搜索顺序回落——
# 本模块 test/ 下不得新建 state/ 或 utils/ 子目录（Step 1 已核）。
target_include_directories(bcos-evm-ref-eth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

③ opstack 源列表 12 行全部加前缀：`bcos-evm-ref/opstack/OpForkSchedule.cpp` … `bcos-evm-ref/opstack/OpBlockFinalize.cpp`（顺序不变）。

- [ ] **Step 4: manifest.tsv 路径列**

8 个条目的 `ref_file` 列加前缀（行号列全部不变——文件内容未动）：`opstack/OpTransition.cpp` → `bcos-evm-ref/opstack/OpTransition.cpp`（auth_constants/auth_list_shared/transition_buy_gas 三条）、`opstack/OpExecCommon.cpp` → `bcos-evm-ref/opstack/OpExecCommon.cpp`（build_message/exec_common_body 两条）、`opstack/OpHost.cpp` → `bcos-evm-ref/opstack/OpHost.cpp`（apply_call_value/call_depth_guard/call_03_quirk 三条）。头部注释「OpTransition.cpp (evmone test/state/state.cpp)」两行同步。golden/ 不动。

- [ ] **Step 5: README.md:7 一句**

`` `eth/` 为 OpStack 与纯 ETH 的共享内核，`opstack/` 在其之上实现 OP 薄层。`` → `` `bcos-evm-ref/eth/` 为 OpStack 与纯 ETH 的共享内核，`bcos-evm-ref/opstack/` 在其之上实现 OP 薄层（仓库主流双名布局，头源同居）。``

- [ ] **Step 6: 重建验证**

```bash
rm -rf build && cmake -B build -S . && cmake --build build -j 8
./build/test/bcos-evm-ref-opstack-tests    # Expected: 85/85 PASS
./build/test/bcos-evm-ref-eth-tests        # Expected: 9 PASS / 3 env-SKIP
scripts/upstream-diff.sh                   # Expected: 8/8 segments OK
rtk git diff --stat --cached 2>/dev/null; rtk git status --short . | grep -v "^R" | grep "\.h\|\.cpp" && echo "STOP: 有非 rename 的源文件改动" || echo "源文件纯 rename ✓"
```

（build-asan/ 为本地目录，需要时同法重建，不阻塞提交。）

- [ ] **Step 7: Commit**

```bash
rtk git add -A .
rtk git commit -m "refactor(evm-ref): 布局对齐仓库主流双名约定（bcos-evm-ref/bcos-evm-ref/ 头源同居）

- include/bcos-evm-ref/* 与顶层 eth/ opstack/ 源目录合并为 bcos-evm-ref/<域>/（git mv，零内容改动）
- include root: include/ → 模块根；全部 #include <bcos-evm-ref/…> 零改动
- CMake 源路径/manifest.tsv ref_file 列/README 同步；防遮蔽核验（test/ 无 state|utils 子目录）"
```

## 已知风险与注意事项

1. **遮蔽风险**（Step 1/3 注释处理）：include root 含 `test/` 目录，evmone `<test/state/…>` 依赖「本模块 test/ 下无同名子目录 → 回落到 evmone include」；这是双名布局在本模块的特有约束（主流模块不消费 evmone `<test/…>` 头），已写入 CMake 注释固化。
2. **上层集成**：模块以 `add_subdirectory` 或 standalone 两种方式构建（`BCOS_EVM_REF_STANDALONE`）；本迁移只动模块内相对路径，两种方式均不受影响。
3. **不做**：README:8 的「生产编排仍留 bcos-evm/opstack」措辞是 D5 前旧文，属文档口径问题非布局问题，不在本 plan（可另记）。
4. 若 clang-format/hook 对 mv 后文件重排：不应发生（内容未动）；发生即违反 Global Constraints 第 1 条，停下检查。
