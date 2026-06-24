# bcos-evm 分层架构重构 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完成 TE + bcos-evm/eth 路径的 evmone-exec 式分层：`EthHost::call()` 递归、策略纯化、三轨编排、CMake 模块隔离。

**Architecture:** 策略 A — `EthHost::call()` 为唯一帧级递归点；`executeViaHost` 仅做 tx 级编排。链扩展经 `HostExtension` 注入。DAG/旧 executive 不改动。

**Tech Stack:** C++20, CMake 3.28+, evmone 0.21, Boost.Test, ctest

**Spec:** [2026-06-18-bcos-evm-layer-refactor-design.md](../specs/2026-06-18-bcos-evm-layer-refactor-design.md)

## Global Constraints

- **Scope:** `bcos-evm/` + `transaction-executor/` 接线 only；**不修改** `bcos-executor` DAG/HostContext/Eip2929*
- **Nested CALL:** 策略 A — `EthHost::call()` 内 `vm.execute()` 递归；**禁止** 帧级重入 `executeViaHost` / `externalCaller`
- **Step 1 boundary:** 仅 `eth/` + eth 测试；**不修改** `ExecuteViaHost.cpp`（Step 2）
- **Regression:** `CompatExecuteViaHost` 50/50（Step 2 后须走真实 `EthHost::call()`）；`FIB101_102_103_104` 10/10
- **RTK:** 所有 shell 命令前缀 `rtk`
- **Commits:** 每 Task 结束单独 commit；用户未要求不 push

---

## File Map（全计划）

| 文件 | Step | 职责 |
|------|------|------|
| `eth/policy/HostExtension.h` | 1 | 钩子重命名 |
| `eth/state/EthHost.hpp/.cpp` | 1 | 递归 call、BlockHashes、routeCall 瘦身 |
| `eth/state/EthPrecompiles.hpp/.cpp` | 1 | `tryDispatchInCall` |
| `eth/state/Transition.cpp` | 1,3 | VM/block_hashes 接线；薄包装 |
| `bcos/FiscoHostExtension.h/.cpp` | 1,2 | `tryChainPrecompile` 迁移 |
| `eth/executeMessage.h/.cpp` | 2,3 | 纯 eth 编排核心 |
| `eth/RevisionConfig.h` | 2 | 纯化 |
| `bcos/FiscoRevisionConfig.h` | 2 | FISCO overlay |
| `bcos/FiscoPolicy.h` | 2 | 返回 overlay |
| `bcos/ExecuteViaHost.h/.cpp` | 2,3 | FISCO 外壳 |
| `bcos/FiscoConstants.h` | 2 | 替代 executor Common.h |
| `bcos/ExecutiveWrapper.h` → `transaction-executor/` | 2 | 移出 bcos-evm |
| `transaction-executor/.../TransactionExecutorImpl.h` | 2 | 删 externalCaller |
| `transaction-executor/tests/ExecuteViaHostEip2929Harness.h` | 2 | 真实 call 路径 |
| `opstack/OpHostExtension.h` | 3 | OP stub |
| `opstack/OpStackExecuteViaHost.h` | 3 | OP 入口 |
| `CMakeLists.txt` (bcos-evm) | 4 | 三库拆分 |
| `include/bcos-evm/*.hpp` | 4 | 公共 API facade |

---

## Step 1：EthHost 递归 call（仅 eth/）

### Task 1: 重命名 HostExtension 钩子

**Files:**
- Modify: `bcos-evm/eth/policy/HostExtension.h`
- Modify: `bcos-evm/bcos/FiscoHostExtension.h`
- Modify: `bcos-evm/bcos/FiscoHostExtension.cpp`
- Modify: `bcos-evm/eth/state/EthHost.cpp`
- Modify: `bcos-evm/eth/policy/EthHostExtension.h`（如有引用）
- Test: `bcos-evm/test/EthHostExtensionHooksTest.cpp`

**Interfaces:**
- Produces: `HostExtension::tryChainPrecompile(rev, msg) -> optional<evmc_result>`
- Produces: `HostExtension::prepareMessage(rev, msg) -> void`

- [ ] **Step 1:** 在 `HostExtension.h` 重命名虚函数：

```cpp
virtual std::optional<evmc_result> tryChainPrecompile(
    evmc_revision rev, const evmc_message& msg) { return std::nullopt; }
virtual void prepareMessage(evmc_revision rev, const evmc_message& msg) { (void)rev; (void)msg; }
```

- [ ] **Step 2:** 全局替换 `callFiscoPrecompile` → `tryChainPrecompile`，`onCreateFrameEntry` → `prepareMessage`（`FiscoHostExtension`、`EthHost.cpp`）

- [ ] **Step 3:** 构建并运行 hooks 测试

```bash
cmake --build build --target EthHostExtensionHooksTest -j$(sysctl -n hw.ncpu)
ctest --test-dir build/bcos-evm/test -R EthHostExtensionHooks --output-on-failure
```

Expected: PASS

- [ ] **Step 4:** Commit

```bash
rtk git add bcos-evm/eth/policy/HostExtension.h bcos-evm/bcos/FiscoHostExtension.* bcos-evm/eth/state/EthHost.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(eth): rename HostExtension hooks to tryChainPrecompile/prepareMessage

EOF
)"
```

---

### Task 2: EthHost 构造扩展（VM + BlockHashes）

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.hpp`
- Modify: `bcos-evm/eth/state/EthHost.cpp`（构造签名）
- Modify: `bcos-evm/eth/state/Transition.cpp`
- Modify: 所有直接构造 `EthHost` 的测试文件

**Interfaces:**
- Produces: `EthHost(State&, evmc_tx_context, evmc_revision, evmc::VM*, BlockHashes, HostExtension*, bool fixStorageStatus)`

- [ ] **Step 1:** 更新 `EthHost.hpp` 成员与构造：

```cpp
EthHost(State& state, evmc_tx_context txContext, evmc_revision revision,
    evmc::VM& vm, BlockHashes blockHashes,
    HostExtension* extension = nullptr, bool fixStorageStatus = true);
// private:
evmc::VM* m_vm{nullptr};
BlockHashes m_blockHashes;
```

- [ ] **Step 2:** 实现 `get_block_hash`：

```cpp
EthHost::bytes32 EthHost::get_block_hash(int64_t number) const noexcept
{
    if (!m_blockHashes) return {};
    return m_blockHashes(number);
}
```

- [ ] **Step 3:** 更新 `Transition.cpp` 传 `vm` 与 `block_hashes`：

```cpp
EthHost host(state, txContext, rev, vm, block_hashes, ext);
```

- [ ] **Step 4:** 修复编译错误（`Eip2929AccessHostTest.cpp`、`StateHostSmokeTest.cpp` 等传 `vm` + stub `BlockHashes`）

```bash
cmake --build build --target bcos-evm -j$(sysctl -n hw.ncpu)
ctest --test-dir build/bcos-evm/test --output-on-failure
```

Expected: 现有测试 PASS（call 仍为 stub）

- [ ] **Step 5:** Commit

---

### Task 3: 嵌套 CALL 失败测试（TDD）

**Files:**
- Create: `bcos-evm/test/state/NestedCallHostTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `EthHost` + `State` + `InMemoryStateView` + evmone VM

- [ ] **Step 1:** 编写测试 — runner 合约 `PUSH20 <B> GAS CALL`，callee 返回 `0x42`：

```cpp
// runner code: call address B, expect returndata 0x42
// callee B code: PUSH1 0x42 PUSH1 0 MSTORE PUSH1 1 PUSH1 31 RETURN
BOOST_AUTO_TEST_CASE(nested_call_returns_callee_output)
{
    InMemoryStateView view;
    // fund runner + insert runner bytecode + insert callee bytecode at 0x02
    evmc::VM vm{evmc_create_evmone()};
    State state(view);
    evmc_tx_context ctx{};
    EthHost host(state, ctx, EVMC_CANCUN, vm, [](int64_t) { return evmc_bytes32{}; }, nullptr);
    // top-level execute runner via vm.execute(host, ...) — NOT CompatHostShim
    // BOOST_CHECK output contains 0x42
}
```

- [ ] **Step 2:** 注册 CMake target `NestedCallHostTest`

- [ ] **Step 3:** 运行确认 FAIL（stub call 不执行 callee）

```bash
cmake --build build --target NestedCallHostTest -j$(sysctl -n hw.ncpu)
ctest --test-dir build/bcos-evm/test -R NestedCallHost --output-on-failure
```

Expected: FAIL

- [ ] **Step 4:** Commit 测试文件

---

### Task 4: 实现 EthHost::call() 递归

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.cpp`
- Modify: `bcos-evm/eth/state/EthHost.hpp`（如需 `resolveCode` 辅助）

**Interfaces:**
- Consumes: `m_vm`, `m_state.checkpoint/revert/commit`
- Produces: 完整 `EthHost::call()` 按 spec §14 顺序

- [ ] **Step 1:** `routeCall()` 移除 `m_extension->onCreateFrameEntry` / `prepareMessage` 调用；保留 CREATE 地址对齐 + `pin_warm_create_address`

- [ ] **Step 2:** 实现 `call()` 主体：

```cpp
EthHost::Result EthHost::call(const evmc_message& msg) noexcept
{
    auto routed = routeCall(msg);
    auto& callMessage = routed.message;

    if (m_extension != nullptr) {
        if (auto r = m_extension->tryChainPrecompile(m_revision, callMessage))
            return Result(*r);
    }
    if (routed.hasPrecompileTarget) {
        if (auto r = EthPrecompiles::tryDispatchInCall(
                routed.precompileTarget, callMessage, m_revision))
            return evmcResultFromPrecompile(*r, callMessage.gas);
    }
    if (callMessage.kind == EVMC_DELEGATECALL && routed.hasPrecompileTarget
        && m_extension != nullptr && !m_extension->allowDelegateCallToPrecompile())
        return makeResult(EVMC_PRECOMPILE_FAILURE, callMessage.gas);

    if (m_extension != nullptr)
        m_extension->prepareMessage(m_revision, callMessage);

    if (!transferValue(callMessage))
        return makeResult(EVMC_INSUFFICIENT_BALANCE, 0);

    auto code = resolveExecutionCode(callMessage);
    m_state.checkpoint();
    auto result = m_vm->execute(*this, m_revision, callMessage, code.data(), code.size());
    if (result.status_code == EVMC_SUCCESS)
        m_state.commit();
    else
        m_state.revert();
    return result;
}
```

- [ ] **Step 3:** 实现 `resolveExecutionCode`（CREATE 用 input；CALL 用 `state.get_code`）

- [ ] **Step 4:** 运行 NestedCallHostTest

```bash
ctest --test-dir build/bcos-evm/test -R NestedCallHost --output-on-failure
```

Expected: PASS

- [ ] **Step 5:** 全量 bcos-evm 测试

```bash
ctest --test-dir build/bcos-evm/test --output-on-failure
```

- [ ] **Step 6:** Commit

---

### Task 5: EthPrecompiles::tryDispatchInCall + 测试

**Files:**
- Modify: `bcos-evm/eth/state/EthPrecompiles.hpp/.cpp`
- Create: `bcos-evm/test/state/PrecompileInCallTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`
- Modify: `bcos-evm/eth/state/Transition.cpp`（顶层 dispatch 可委托 `tryDispatchInCall` 去重）

**Interfaces:**
- Produces: `EthPrecompiles::tryDispatchInCall(addr, msg, rev) -> optional<EthPrecompileResult>`

- [ ] **Step 1:** 提取 `dispatch` 核心逻辑到 `tryDispatchInCall`，顶层 `transition` 复用

- [ ] **Step 2:** 测试 — 合约 bytecode `PUSH20 0x01 GAS CALL` 调 ecrecover（或 identity 0x04）

- [ ] **Step 3:** `ctest -R PrecompileInCall` PASS

- [ ] **Step 4:** Commit

---

### Task 6: BLOCKHASH 测试

**Files:**
- Create: `bcos-evm/test/state/BlockHashHostTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

- [ ] **Step 1:** 测试传入 `BlockHashes` lambda 返回已知 hash；bytecode 执行 `BLOCKHASH`

- [ ] **Step 2:** `ctest -R BlockHashHost` PASS

- [ ] **Step 3:** Commit

---

### Task 7: 子帧 REVERT warm set 测试

**Files:**
- Create: `bcos-evm/test/state/NestedRevertWarmTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

- [ ] **Step 1:** runner 调 callee（callee REVERT）；验证父帧已 warm 的地址在子帧 revert 后仍 warm

- [ ] **Step 2:** 通过 `host.access_account` / `State::is_address_warm` 断言

- [ ] **Step 3:** `ctest -R NestedRevertWarm` PASS

- [ ] **Step 4:** Commit

---

### Task 8: FISCO [PRECOMPILED] 路由迁入 tryChainPrecompile

**Files:**
- Modify: `bcos-evm/bcos/FiscoHostExtension.cpp`
- Modify: `bcos-evm/eth/state/EthHost.cpp`（删除 `parseDynamicPrecompileTarget`）
- Test: `bcos-evm/test/FiscoHostExtensionTest.cpp`

- [ ] **Step 1:** 将 `parseDynamicPrecompileTarget` 逻辑移入 `FiscoHostExtension::tryChainPrecompile`（读 code 中 `[PRECOMPILED]` 标记）

- [ ] **Step 2:** `routeCall` 不再解析 `[PRECOMPILED]`；仅看 `code_address` 是否 builtin

- [ ] **Step 3:** `ctest -R FiscoHostExtension` PASS

- [ ] **Step 4:** Commit

---

### Task 9: Step 1 验收门禁

- [ ] **Step 1:** 全量回归

```bash
ctest --test-dir build/bcos-evm/test --output-on-failure
# 期望 ≥13 tests PASS（原 9 + NestedCall + PrecompileInCall + BlockHash + NestedRevertWarm）
```

- [ ] **Step 2:** 确认 **未修改** `bcos-evm/bcos/ExecuteViaHost.cpp`（`git diff --name-only` 检查）

- [ ] **Step 3:** 更新 `sdd/progress.md` 标记 Step 1 complete

- [ ] **Step 4:** Commit progress

---

## Step 2：策略纯化 + executeMessage + TE 接线

### Task 10: FiscoRevisionConfig 与 RevisionConfig 纯化

**Files:**
- Modify: `bcos-evm/eth/RevisionConfig.h`
- Create: `bcos-evm/bcos/FiscoRevisionConfig.h`
- Modify: `bcos-evm/bcos/FiscoPolicy.h`
- Modify: `bcos-evm/eth/vm/EthPolicy.h`
- Modify: 所有使用 `fix_*` / `enable_*` 的 TE/测试文件

**Interfaces:**
- Produces: `struct FiscoRevisionConfig { RevisionConfig eth; bool fix_storage_status; ... };`
- Produces: `RevisionConfig FiscoRevisionConfig::eth() const { return eth; }`

- [ ] **Step 1:** 从 `RevisionConfig` 移除 C/D 段字段

- [ ] **Step 2:** 创建 `FiscoRevisionConfig` 含全部 `fix_*`、`enable_*`

- [ ] **Step 3:** `FiscoPolicy::computeRevisionConfig` 返回 `FiscoRevisionConfig`

- [ ] **Step 4:** TE `TransactionExecutorImpl` / `ExecuteViaHostInput` 改用 `FiscoRevisionConfig`；传 `eth()` 给 eth 层

- [ ] **Step 5:** 构建 + `ctest -R 'TestFiscoPolicy|TestStandardEthPolicy'` PASS

- [ ] **Step 6:** CI grep：`! grep -r 'bcos-executor' bcos-evm/eth/`

- [ ] **Step 7:** Commit

---

### Task 11: eth::executeMessage 抽出

**Files:**
- Create: `bcos-evm/eth/executeMessage.h`
- Create: `bcos-evm/eth/executeMessage.cpp`
- Modify: `bcos-evm/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct ExecuteMessageInput {
    StateView const* stateView;
    evmc::VM* vm;
    evmc_message message;
    BlockInfo blockInfo;
    BlockHashes blockHashes;
    RevisionConfig revisionConfig;
    TransactionProperties txProps;
    const Eip2930AccessList* accessList{nullptr};
    uint8_t web3TypedTxKind{0};
    HostExtension* extension{nullptr};
    bool fixStorageStatus{true};
};
struct ExecuteMessageOutput {
    evmc::Result result;
    StateDiff stateDiff;
    std::vector<LogEntry> logs;
};
ExecuteMessageOutput executeMessage(ExecuteMessageInput input);
```

- [ ] **Step 1:** 从 `ExecuteViaHost.cpp` 提取 warm + checkpoint + EthHost(vm) + vm.execute + commit/revert 逻辑

- [ ] **Step 2:** 单元测试：用 `InMemoryStateView` 调 `executeMessage` 跑简单 CALL

- [ ] **Step 3:** Commit

---

### Task 12: ExecuteViaHost 重构 + transition 薄包装

**Files:**
- Modify: `bcos-evm/bcos/ExecuteViaHost.cpp`
- Modify: `bcos-evm/bcos/ExecuteViaHost.h`
- Modify: `bcos-evm/eth/state/Transition.cpp`

- [ ] **Step 1:** `executeViaHost` 保留 deriveMessage、auth、EIP-7623、FiscoHostExtension 注入；核心调 `executeMessage`

- [ ] **Step 2:** `EthHost` 构造传入 `input.vm` + `input.blockHashes`

- [ ] **Step 3:** `transition()` 改为调用 `executeMessage` + receipt 转换

- [ ] **Step 4:** `ctest --test-dir build/bcos-evm/test` + `ExecuteViaHostCompat` PASS

- [ ] **Step 5:** Commit

---

### Task 13: FiscoConstants + 去除 bcos-executor 依赖

**Files:**
- Create: `bcos-evm/bcos/FiscoConstants.h`
- Modify: `bcos-evm/bcos/FiscoHostExtension.h/.cpp`
- Modify: `bcos-evm/bcos/ExecuteViaHost.cpp`（`BALANCE_TRANSFER_GAS` 等常量）

- [ ] **Step 1:** 从 `bcos-executor/src/Common.h` 迁移所需常量到 `FiscoConstants.h`

- [ ] **Step 2:** 替换 include；`! grep -r 'bcos-executor' bcos-evm/` 通过

- [ ] **Step 3:** Commit

---

### Task 14: ExecutiveWrapper 上移 + 删除 TE externalCaller

**Files:**
- Move: `bcos-evm/bcos/ExecutiveWrapper.h` → `transaction-executor/bcos-transaction-executor/ExecutiveWrapper.h`
- Modify: `transaction-executor/.../TransactionExecutorImpl.h`
- Modify: `transaction-executor/tests/ExecuteViaHostEip2929Harness.h`

- [ ] **Step 1:** 移动文件，更新 include 路径

- [ ] **Step 2:** 删除 `TransactionExecutorImpl` 中 stub `externalCaller`；`checkAuth` 不再需要 externalCaller 参数（或传 no-op 仅用于 auth 内部 precompile）

- [ ] **Step 3:** 改造 `CompatHostShim`：`execute()` / `externalCall()` 统一走 `EthHost::call()` 递归（删除 `runEvm` 旁路，或 `runEvm` 仅构建 EthHost+顶层 execute）

- [ ] **Step 4:** `ctest -R 'CompatExecuteViaHost'` 50/50 PASS

- [ ] **Step 5:** `ctest -R 'ExecuteViaHostCompat|FIB101'` PASS

- [ ] **Step 6:** Commit

---

### Task 15: Step 2 验收

```bash
! grep -r 'bcos-executor' bcos-evm/
ctest -R 'CompatExecuteViaHost'   # 50/50
ctest -R 'ExecuteViaHostCompat|FIB101_102_103_104_SchedulerTest'
```

- [ ] Commit `sdd/progress.md` Step 2 complete

---

## Step 3：三轨编排 + OpStack stub

### Task 16: OpHostExtension + OpStackExecuteViaHost

**Files:**
- Create: `bcos-evm/opstack/OpHostExtension.h`
- Create: `bcos-evm/opstack/OpStackExecuteViaHost.h`
- Create: `bcos-evm/test/opstack/OpStackExecuteViaHostSmokeTest.cpp`
- Modify: `bcos-evm/opstack/OpStackTxExecutor.h`
- Modify: `bcos-evm/CMakeLists.txt`

- [ ] **Step 1:** `OpHostExtension : HostExtension` — `prepareMessage` no-op stub；`tryChainPrecompile` 预留 L1Block 地址

- [ ] **Step 2:** `OpStackExecuteViaHost` 包装 `executeMessage` + `OpStackTxExecutor::buyGas/refundGas`

- [ ] **Step 3:** 烟雾测试 ≥3 cases（L1 fee 扣款断言）

- [ ] **Step 4:** `ctest -R OpStackExecuteViaHost` PASS

- [ ] **Step 5:** Commit

---

### Task 17: EthTxExecutor 接入 + transition 最终收敛

**Files:**
- Modify: `bcos-evm/eth/EthTxExecutor.h`
- Modify: `bcos-evm/eth/state/Transition.cpp`

- [ ] **Step 1:** 确认 `transition` / `executeMessage` / `executeViaHost` 无重复 warm 逻辑

- [ ] **Step 2:** 全量回归 Step 3 门禁

- [ ] **Step 3:** Commit

---

## Step 4：CMake 模块拆分 + 公共 API

### Task 18: 三库 CMake 拆分

**Files:**
- Modify: `bcos-evm/CMakeLists.txt`
- Modify: 依赖 `bcos-evm` 的下游 `CMakeLists.txt`（`transaction-executor`、`bcos-executor` 等）

- [ ] **Step 1:** 定义 `bcos-evm-eth`、`bcos-evm-bcos`、`bcos-evm-op` 源文件列表

- [ ] **Step 2:** `add_library(bcos-evm ALIAS bcos-evm-bcos)`

- [ ] **Step 3:** 独立编译三门

```bash
cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op -j$(sysctl -n hw.ncpu)
```

- [ ] **Step 4:** Commit

---

### Task 19: 公共 API facade 头

**Files:**
- Create: `include/bcos-evm/executor.hpp`
- Create: `include/bcos-evm/eth_executor.hpp`
- Create: `include/bcos-evm/fisco_executor.hpp`
- Create: `include/bcos-evm/op_executor.hpp`
- Modify: 根 `CMakeLists.txt` install/export（如已有 pattern 则跟随）

- [ ] **Step 1:** 各头文件 `#include` 转发到现有 `bcos-evm/eth/...` 路径

- [ ] **Step 2:** 编译 smoke include 测试或文档注释

- [ ] **Step 3:** Commit

---

### Task 20: 最终验收 + progress

```bash
ctest --test-dir build/bcos-evm/test --output-on-failure
ctest -R 'CompatExecuteViaHost|ExecuteViaHostCompat|FIB101|OpStackExecuteViaHost'
cmake --build build --target test-transaction-executor -j$(sysctl -n hw.ncpu)
! grep -r 'bcos-executor' bcos-evm/
```

- [ ] 更新 `sdd/progress.md`：Layer Refactor Step 1–4 complete

- [ ] Commit

---

## Plan Self-Review

| Spec 章节 | 对应 Task |
|-----------|-----------|
| §4 Step 1 递归 call | Task 1–9 |
| §5 Step 2 RevisionConfig | Task 10–15 |
| §6 Step 3 三轨 | Task 16–17 |
| §7 Step 4 CMake | Task 18–19 |
| §13 策略 A | Task 4, 14 |
| §14 钩子顺序 | Task 4, 8 |
| §2.2 排除 DAG | Global Constraints |
| §10 回归基线 | Task 9, 15, 20 |

**Placeholder scan:** 无 TBD/TODO。  
**Type consistency:** `tryChainPrecompile` / `prepareMessage` / `FiscoRevisionConfig` 全篇一致。

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-18-bcos-evm-layer-refactor.md`.

**两种执行方式：**

1. **Subagent-Driven（推荐）** — 每 Task 派生子 agent，Task 间 review  
2. **Inline Execution** — 本会话按 Task 顺序直接实现，checkpoint 验收

**请选择执行方式，或指定从 Task 1 开始。**
