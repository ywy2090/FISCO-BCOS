# Geth Naming P0–P6 Plan

**Branch:** `feat/adr-030-geth-naming`  
**Baseline:** `8c3ced3fe`  
**Authority:** ADR-029, ADR-030

## Global Constraints

- Never remove Tier E symbols in P1–P4; only add deprecated aliases when promoting geth names.
- `eth/` headers MUST NOT `#include` `bcos/` or `opstack/`.
- Pre-commit clang-format must pass.
- Each task: focused ctest + commit; report to `.superpowers/sdd/task-N-report.md`.
- Do not commit `.superpowers/sdd/` artifacts.

---

## Task 1 (P0): Fix EthOrchestrationProfile test

**Problem:** `EthOrchestrationProfileTest::pre_execute_precheck_early_exit` fails because `ethTxPrecheck` gates fee-cap validation on `isEip1559FeeMarketActive(revisionConfig)` but test leaves `eip1559` false.

**Fix:**
1. In `pre_execute_precheck_early_exit`, set `input.revisionConfig.eip1559 = true` (and `hasExplicitFeeCaps = true` if needed).
2. Verify `EthFrameParityHelpers.h` `Depth1HostFixture` passes `nullptr` for `ChainPrecompileDispatch*` (not `bool false`) — fix if compile broken.
3. Run: `ctest -R 'EthOrchestrationProfile|TxExecutionRunner'`

**Acceptance:** both tests PASS.

**Commit:** `fix(bcos-evm): align EthOrchestrationProfile precheck test with EIP-1559 gate`

---

## Task 2 (P1): Phase 3b — kernel canonical names + TE ADR

**2a — ADR:** Add `bcos-evm/docs/adr/031-te-geth-kernel-symbol-migration.md` documenting Tier E → geth promotion schedule.

**2b — eth kernel:** Promote geth names to canonical:
- Rename `runTxPipeline` function → `stateTransitionExecute`; add `[[deprecated]] inline runTxPipeline` forwarding.
- Rename `executeMessage` → `innerExecute` in `ExecuteMessage.h/.cpp`; add `[[deprecated]] inline executeMessage` forwarding.
- Update `GethNamingAliases.h` to reflect canonical names (remove duplicate forwards if redundant).
- Update internal call sites in bcos-evm to use canonical names.

**2c — TE:** Update `transaction-executor` to call `stateTransitionExecute` / `innerExecute` where it directly references pipeline/kernel (grep `runTxPipeline`, `executeMessage`).

**Tests:** `ctest -R 'GethNaming|TxPipeline|TxExecutionRunner|EthOrchestrationProfile'`

**Commit:** `refactor(bcos-evm): promote stateTransitionExecute and innerExecute to canonical kernel names`

---

## Task 3 (P2): Phase 4c — apply*Message as documented primary

- Add comment blocks in chain headers marking `apply*Message` as ADR-030 canonical / `*Execute` as Tier E stable.
- Update TE impls to prefer `applyReferenceMessage` / `applyFiscoMessage` / `applyOpStackMessage` in new code paths (grep replace in transaction-executor only).
- Update `eth/README.md` and `architecture-overview.md` entry tables: canonical column first.
- Do NOT add `[[deprecated]]` on `*Execute` yet.

**Commit:** `refactor(transaction-executor): prefer apply*Message over Tier E execute entry names`

---

## Task 4 (P3): OpStack lifecycleCheckEntryRules

Rename per ADR-029 §5:
- `OpStackPrecheckPolicy::checkEntryRules` → `lifecycleCheckEntryRules`
- Deprecated inline `checkEntryRules` alias
- Update `OpStackTxLifecycle.cpp`, tests, opstack README

**Commit:** `refactor(opstack): rename checkEntryRules to lifecycleCheckEntryRules per ADR-029`

---

## Task 5 (P4): Phase 5 — orchestration type aliases (minimal)

Add **documentation-only type aliases** in a new header `bcos-evm/eth/GethOrchestrationAliases.h` (no mass rename):

```cpp
using StateTransitionHooks = EthOrchestrationProfile; // example pattern per chain
```

Only add aliases for types that have 1:1 mapping in ADR-030 §6; do NOT rename files/classes.

**Commit:** `docs(bcos-evm): add GethOrchestrationAliases documentation type aliases`

---

## Task 6 (P5): Tier E retirement ADR (plan only)

Extend ADR-031 or add `032-tier-e-retirement.md` with:
- Symbols to remove and order
- TE migration checklist
- No code removal in this task

**Commit:** `docs(bcos-evm): add ADR for Tier E symbol retirement schedule`

---

## Task 7 (P6): Doc sweep + test rename

1. Fix `architecture-overview.md` mermaid/§3.1: `runEvmKernelTopLevel` / `runCallFrame` (not old names).
2. Rename ctest target `DebitIntrinsicGas` → `DeductIntrinsicGas` in EthTests.cmake (and test file if named Debit*).
3. Batch-replace `eth/reference/` → `eth/apply/` in `bcos-evm/docs/` only (not docs/superpowers/).

**Commit:** `chore(bcos-evm): sync architecture docs and deductIntrinsicGas test naming`
