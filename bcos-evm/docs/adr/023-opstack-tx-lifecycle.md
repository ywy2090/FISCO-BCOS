# ADR-023: OpStack Transaction Lifecycle (Deep Module)

**Status:** Accepted (C0–C3 complete; fee projection Appendix A PR1–PR3 complete)  
**Date:** 2026-06-25 (Appendix A alignment 2026-06-26)  
**Related:** ADR-019, ADR-021 (Appendix A), ADR-025, `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md`

---

## Context

ADR-021 deepened **settlement math** (`finalizeNormal` / `finalizeDeposit`) and async settlement. **ADR-021 Appendix A (2026-06-26)** replaced `OpStackFeeContext` / `populateFeeContext` with `OpStackSettlementView` + `OpStackFeeSidecar` and moved normal post-pipeline wiring into **`OpStackNormalFeeSettlement`** (`buyGas` + `completeAfterPipeline`). `TxPipelineContext` remains the gas/message/state single source.

Historical context (pre–Appendix A): outer-ring wiring lived in `OpStackExecutionBridge` with manual `OpStackFeeContext` initialization and public `settleNormal`.

Grilling decisions D12–D18 (2026-06-25) resolved interface shape and delivery phasing.

---

## Decision

### 1. Deep module: `OpStackTxLifecycle`

Introduce `runOpStackTxLifecycle(OpStackExecutionRequest)` as the **OpStack outer-ring deep module** in `opstack/`.

`opStackExecute` remains the **TE-stable adapter**: validate `stateView` / `vm` / `hashImpl`, then delegate to lifecycle.

| Export | Role |
| --- | --- |
| `OpStackTxLifecycle.h` → `runOpStackTxLifecycle` | Deep module interface; primary characterization test surface after C1 |
| `OpStackExecutionBridge.h` → `opStackExecute` | Stable TE seam |

### 2. Compose ADR-021 — do not merge

| Module | Responsibility | Lifecycle relationship |
| --- | --- | --- |
| `runTxPipeline` (eth) | Fixed 12-step kernel | lifecycle calls via `OpStackOrchestrationProfile::bind` |
| `finalizeNormal` / `finalizeDeposit` | Sync gas/journal math | normal: internal to `OpStackNormalFeeSettlement`; deposit: via `settleDeposit` |
| `OpStackNormalFeeSettlement` | `buyGas` + `completeAfterPipeline` (ADR-025 tree) | lifecycle sole normal settlement interface |
| `settleDeposit` | Async: `finalizeDeposit` + gasPool | lifecycle sole deposit post-pipeline entry |
| `OpStackTxFeeLedger` | buyGas / refundGas adapter | called via settlement module / `settleDeposit`; not on bridge |

ADR-021 invariants unchanged.

### 3. Internal session (not interface)

`OpStackOrchestrationProfile::Session` (internal):

- `OpStackExecutionRequest& input`
- `OpStackSettlementView view` (`ctx` + `input` + `sidecar`)

`sidecar` / `view` do not cross the lifecycle external seam.

### 4. Lifecycle step order

```text
view = { ctx, input, sidecar }
session = { input, view }
Profile::bind(session)   // once
OpStackPrecheckPolicy::checkEntryRules(ctx)
branch:
  deposit:
    acquireGasPool
    depositNonce → mint → checkpoint
    runTxPipeline
    settleDeposit → stateDiff
  normal:
    acquireGasPool → checkpoint
    NormalFeeSettlement.buyGas → on fail: abortNormalAfterBuyGas
    runTxPipeline
    NormalFeeSettlement.completeAfterPipeline  // ADR-025 abort or commit+settle+meta
```

### 4½. OpStack sync precheck module (C3)

All **sync** precheck rules live in `OpStackPrecheckPolicy` (two phases):

| Phase | Method | When | Rules |
| --- | --- | --- | --- |
| Entry | `checkEntryRules` | lifecycle, before `buyGas` | nonce, 7702 sender, EIP-1559 caps, blob intent, auth+CREATE |
| Gas afford | `checkGasAffordable` | `runTxPipeline`, after `buyGas` (normal) | floor data gas, `canTransfer(value)` on post-debit balance |

`checkTransactionRules` remains **no-op** for OpStack (pipeline step ②). Async `buyGas`/`refundGas`/`settle*` stay in lifecycle per ADR-019 Q7.

Deleted: `opStackTxPrecheck` free function. `isDepositTx()` → `OpStackDepositTx.h`.

### 5. GasPool ownership (C2)

Lifecycle is the **only** module that calls `gasPool.subGas` / `gasPool.returnGas` (including buyGas-failure abort). Precheck becomes side-effect-free.

Private helpers: `acquireGasPool`, `releaseGasPool`.

### 6. Profile wiring

Lifecycle **inlines** `OpStackOrchestrationProfile::bind(session)` — hooks are not injected through the external interface (D13).

### 7. Early-exit result contract (D18)

| Path | Guaranteed fields | Not guaranteed |
| --- | --- | --- |
| precheck failure | `evmcResult` | `gasUsed`, `stateDiff`, `receiptMeta`, `logs` |
| buyGas / subGas failure | `evmcResult`; gasPool released; `gasUsed = 0` | `receiptMeta`, fee routing in `stateDiff` |
| normal pre-execution reject (`IntrinsicRejected`, `GasAffordRejected`) | `evmcResult`; `gasUsed = 0`; gasPool released; **no fee meta**; **unchanged sender balance in `stateDiff`** | `logs` |
| full path | all via `projectOutput` | — |

See **ADR-025** for R3-7623-1 abort semantics (`buyGas` checkpoint + revert; `completeAfterPipeline` must not settle on entry reject).

### 8. Phased delivery

| Phase | Scope | Status |
| --- | --- | --- |
| **C0** | ADR-023 (this doc) + `OpStackTxLifecycleCharacterizationTest` (6 paths via `opStackExecute`) | Done |
| **C1** | `runOpStackTxLifecycle`; bridge delegate; tests target lifecycle | Done |
| **C2** | Precheck pure; gasPool centralized in lifecycle | Done |
| **C3** | Sync precheck consolidated in `OpStackPrecheckPolicy` (entry + gasAffordable phases) | Done |

### 9. Characterization matrix (C0 oracle)

| # | Scenario | Key assertions |
| --- | --- | --- |
| 1 | Normal SUCCESS | fee routing (sender/coinbase/L1/operator), `gasUsed`, `l1Fee`, gasPool once |
| 2 | Normal IntrinsicRejected | `gasUsed == 0`, gasPool `returnGas(fullLimit, 0)`; sender balance unchanged; no `receiptMeta.l1Fee` |
| 3 | Normal buyGas fail | `NotEnoughCash`, gasPool `returnGas(limit, 0)`, no settle |
| 8 | Normal GasAffordRejected | same abort contract as #2 (ADR-025) |
| 4 | Deposit SUCCESS + mint | mint retained, `depositNonce`, nonce +1 |
| 5 | Deposit REVERT | balance revert, nonce +1, actual `gasUsed` |
| 6 | Deposit IntrinsicRejected | `gasUsed == gasLimit`, balance revert, nonce +1 |
| 7 | Deposit gasPool reject | `EVMC_OUT_OF_GAS`, subGas once, no returnGas, no mint |

---

## Consequences

### Positive

- Single interface for outer-ring OpStack tx story; bridge locality after C1.
- Characterization tests lock combination behavior before refactor.
- ADR-021 settlement layers stay independently testable.

### Negative / trade-offs

- Temporary dual entry (`opStackExecute` + `runOpStackTxLifecycle`) until TE migrates naming (TE keeps `opStackExecute`).
- C2 moves deposit `subGas` from precheck — behavior-equivalent, tests required.

---

## Compliance (C0 reviewers)

- [ ] ADR-023 documents D12–D18 decisions
- [x] `OpStackTxLifecycleCharacterizationTest` — 7 cases green via `runOpStackTxLifecycle`
- [ ] No production code change in C0 (tests + ADR only)
- [ ] `eth/` seam discipline unchanged (ADR-005)

## Compliance (C1 reviewers)

- [x] `runOpStackTxLifecycle` owns bridge body
- [x] `opStackExecute` is validate + delegate only
- [x] Characterization tests call `runOpStackTxLifecycle`
- [x] Zero behavior change vs C0 oracles

## Compliance (C2 reviewers)

- [x] Entry precheck has no `gasPool` side effects
- [x] Lifecycle `acquireGasPool` used for deposit and normal
- [x] Lifecycle `releaseGasPoolFullLimit` on buyGas failure
- [x] ADR-023 Status → Accepted

## Compliance (C3 reviewers)

- [x] `OpStackPrecheckPolicy::checkEntryRules` owns former `opStackTxPrecheck` logic
- [x] Lifecycle bind-once; entry rules before buyGas; pipeline `checkGasAffordable` unchanged
- [x] `OpStackPrecheckPolicyTest` + migrated entry-precheck tests green
- [x] `OpStackTxLifecycleCharacterizationTest` regression green
