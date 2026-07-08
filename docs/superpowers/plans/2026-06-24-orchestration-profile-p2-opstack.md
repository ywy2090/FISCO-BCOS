# OpStack OrchestrationProfile (P2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox syntax.

**Goal:** 将 `OpStackExecuteViaHost.cpp` 内 `buildHooks` lambda 迁入 `OpStackOrchestrationProfile`（Session + `buildHooks`），零语义变更。

**Architecture:** 镜像 P1 Fisco：`Session { input, txData }`；链策略 hook（`preDebitEntry`、`intrinsicPolicy`、`postSettle`、测试 `executeMessageOverride`）进 Profile；`mapIntrinsicFailure` / `mapException` 留 wrapper + TODO candidate 4。`applySettlement` 逻辑迁入 Profile.cpp 匿名 namespace。

**Tech Stack:** C++20 · Boost.Test · bcos-evm-op · ADR-005/019

## Global Constraints

- 不修改 `OpStackExecuteViaHostInput` / `OpStackExecuteViaHostOutput` 布局
- 不新增公开 `include/bcos-evm/` facade
- error hook 不在 Profile 范围（grilling Q8）
- 不合并候选 7 OpStackSettlementContext（txData 影子帧保留）
- 零语义变更：OpStack smoke 全绿
- PascalCase 文件名（ADR-020）

---

### Task 1: Scaffold OpStackOrchestrationProfile + Internals

**Files:**
- Create: `opstack/OpStackOrchestrationInternals.h` — `makeOutOfGasLimitResult()`, `makeInternalErrorResult()`
- Create: `opstack/OpStackOrchestrationProfile.h` — Session + buildHooks 声明
- Create: `opstack/OpStackOrchestrationProfile.cpp` — stub
- Modify: `bcos-evm/CMakeLists.txt` — `BCOS_EVM_OP_SOURCES` 追加 Profile.cpp
- Modify: `opstack/OpStackExecuteViaHost.cpp` — 剪切 error helper 到 Internals，wrapper `#include` Internals

**Session:**
```cpp
struct OpStackOrchestrationProfile {
    struct Session {
        OpStackExecuteViaHostInput const& input;
        OpStackTxExecutor::OpStackTxExecutionData& txData;
    };
    static OrchestrationHooks buildHooks(Session& session);
};
```

- [ ] Build `bcos-evm-op`
- [ ] Commit: `refactor(bcos-evm): scaffold OpStackOrchestrationProfile module`

---

### Task 2: OpStackOrchestrationProfileTest (TDD RED)

**Files:**
- Create: `test/opstack/OpStackOrchestrationProfileTest.cpp`
- Modify: `test/CMakeLists.txt`

**Cases:**
1. `intrinsic_policy_op_stack_entry` — `mode == OpStackEntry`
2. `pre_debit_entry_floor_rejects` — 触发 earlyExit（minimal state + floor）
3. `post_settle_updates_tx_data_gas` — 调用 postSettle 后 `txData.m_gasUsed` 更新

- [ ] ctest RED
- [ ] Commit: `test(bcos-evm): add OpStackOrchestrationProfile hook wiring tests`

---

### Task 3: Implement buildHooks

**Files:**
- Modify: `opstack/OpStackOrchestrationProfile.cpp`

Migrate from `OpStackExecuteViaHost.cpp` L120-179（不含 mapIntrinsicFailure/mapException）:
- `applyOpStackSettlement` helper（原 applySettlement lambda）
- `preDebitEntry`, `intrinsicPolicy`, `postSettle`
- `#ifdef BCOS_EVM_TESTING` `executeMessageOverride`

- [ ] ctest GREEN on OpStackOrchestrationProfile
- [ ] Commit: `feat(bcos-evm): implement OpStackOrchestrationProfile buildHooks`

---

### Task 4: Slim OpStackExecuteViaHost.cpp

Replace local `buildHooks` lambda with:
```cpp
OpStackOrchestrationProfile::Session session{input, txData};
auto hooks = OpStackOrchestrationProfile::buildHooks(session);
// TODO: OrchestrationErrorPolicy (candidate 4)
hooks.mapIntrinsicFailure = ...
hooks.mapException = ...
runOrchestration(ctx, hooks);
```

Both deposit and normal paths use same pattern.

- [ ] Build + ctest P1 trio + OpStackOrchestrationProfile
- [ ] Commit: `refactor(bcos-evm): opStackExecuteViaHost delegates hooks to OpStackOrchestrationProfile`

---

### Task 5: Regression gate

- [ ] `ctest -R 'OpStackOrchestrationProfile|OpStackExecuteViaHostSmoke|OpStackIntrinsicGasSync'`
- [ ] Extended: `ctest -R 'OpStack'`
- [ ] ADR-005: no new bcos includes in eth/
- [ ] Update `.superpowers/sdd/progress.md`
