# Eth OrchestrationProfile (P3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 将 `ExecuteViaEth.cpp` 链策略 hook 迁入 `EthOrchestrationProfile`（Session + `buildHooks`），零语义变更。

**Architecture:** 镜像 P1/P2；`Session { input, output }`；链 hook 进 Profile；`mapIntrinsicFailure` / `mapException` 留 wrapper。**注意：** ExecutionFrame PR2 未落地，`postAdopt` vmerr 逻辑按现状迁入 Profile。

**Tech Stack:** C++20 · Boost.Test · bcos-evm-eth · ADR-005

## Global Constraints

- 不修改 `ExecuteViaEthInput` / `ExecuteViaEthOutput` 布局
- `eth/` 不得 include `bcos/` / `opstack/`
- error hook 不在 Profile
- 零语义变更

---

### Task 1: Scaffold EthOrchestrationProfile

**Files:**
- Create: `eth/EthOrchestrationProfile.h`
- Create: `eth/EthOrchestrationProfile.cpp` (stub)
- Modify: `bcos-evm/CMakeLists.txt` — `BCOS_EVM_ETH_SOURCES`

```cpp
struct EthOrchestrationProfile {
    struct Session {
        ExecuteViaEthInput const& input;
        ExecuteViaEthOutput& output;
    };
    static OrchestrationHooks buildHooks(Session& session);
};
```

- [ ] Build `bcos-evm-eth`
- [ ] Commit: `refactor(bcos-evm): scaffold EthOrchestrationProfile module`

---

### Task 2: EthOrchestrationProfileTest (TDD RED)

**Files:** `test/eth/EthOrchestrationProfileTest.cpp`, `test/CMakeLists.txt`

**Cases:**
1. `intrinsic_policy_eip7623` — revision eip7623 → `Eip7623` mode
2. `intrinsic_policy_auth_only` — auth list present, no 7623 → `AuthOnly`
3. `pre_execute_precheck_early_exit` — mock failing preCheck (or insufficient balance path via preKernel)
4. `post_adopt_sets_included_tx_vmerr_flag` — postAdopt sets `output.topLevelIncludedTxVmError`

- [ ] ctest RED
- [ ] Commit: `test(bcos-evm): add EthOrchestrationProfile hook wiring tests`

---

### Task 3: Implement buildHooks

Migrate from `ExecuteViaEth.cpp` L69-128:
- `preExecute` (preCheck + 1559 gas)
- `intrinsicPolicy`
- `preKernel` (canTransfer)
- `postAdopt` (vmerr normalize)

NO mapIntrinsicFailure / mapException.

- [ ] ctest GREEN
- [ ] Commit: `feat(bcos-evm): implement EthOrchestrationProfile buildHooks`

---

### Task 4: Slim ExecuteViaEth.cpp

```cpp
EthOrchestrationProfile::Session session{input, output};
auto hooks = EthOrchestrationProfile::buildHooks(session);
// TODO: OrchestrationErrorPolicy (candidate 4)
hooks.mapIntrinsicFailure = ...
hooks.mapException = ...
runOrchestration(ctx, hooks);
```

- [ ] Commit: `refactor(bcos-evm): executeViaEth delegates hooks to EthOrchestrationProfile`

---

### Task 5: Regression gate

- [ ] `ctest -R 'EthOrchestrationProfile|EthIncludedTxVmerr|ExecuteMessageSmoke'`
- [ ] Extended eth smoke
- [ ] Fisco/OpStack P1/P2 cross-check still green
- [ ] ADR-005
- [ ] Update progress.md
