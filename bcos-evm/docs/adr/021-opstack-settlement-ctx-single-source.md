# ADR-021: OpStack Settlement — ctx Single Source of Truth

**Status:** Implemented (PR1 + PR2 + Appendix A PR1–PR3)  
**Date:** 2026-06-25 (PR2 section added 2026-06-25; Appendix A 2026-06-26)  
**Implementation:** `OpStackSettlementFacade`, `OpStackFeeSidecar`, `OpStackNormalTxFeeCoordinator`, `finalizeNormal(sidecar)`  
**Spec (PR2):** `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md`  
**Spec (Appendix A):** `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md`  
**Deciders:** bcos-evm architecture  
**Related:** ADR-019, ADR-005, ADR-025, `docs/superpowers/specs/2026-06-25-opstack-settlement-ctx-single-source-design.md`, `bcos-evm/docs/audits/_work/task4-deposit.md`

---

## Context

ADR-019 fixed OpStack intrinsic gas dual-track (`txData.m_message` vs `executeMessage` input). `TxPipelineContext::message` is now the intrinsic debit owner for the shared pipeline.

`OpStackTxExecutionData` duplicated `m_message`, `m_gasLimit`, and `m_state` for `buyGas`, `applySettlement`, and `refundGas`. Settlement read frozen snapshots while the pipeline mutated `ctx.message`. ADR-019 Q14 (`ctx.state` sole owner) was only half satisfied for gas state.

**PR1 delivered** ctx gas/message single-source and sync gas math (`finalizeNormal`). **PR1 did not fully close** settlement locality: `refundGas` and gas pool return remain in `OpStackExecute.cpp`; deposit post-pipeline remains inline. PR2 completes the deep module.

---

## Decision

### 2.1 PR1 — ctx gas/message single source (Implemented)

#### Gas/message truth source (normal L2 txs)

| Concern | Single source |
| --- | --- |
| Message (sender, value, post-intrinsic gas) | `TxPipelineContext::message` |
| Original tx gas budget | `TxPipelineContext::originalGasLimit` |
| EVM outcome | `TxPipelineContext::evmcResult` |
| State mutations | `TxPipelineContext::state` |

`OpStackFeeContext` (narrowed `OpStackTxExecutionData`) holds **fee ledger state only** — no `m_message`, `m_gasLimit`, `m_state`.

#### `finalizeNormal` — sync gas math (PR1)

`OpStackSettlement::finalizeNormal(ctx, feeCtx, exitKind)` computes `gasUsed` / `gasRemaining` / `maxUsedGas` from `ctx` + `exitKind`:

- Early-exit (`IntrinsicRejected`, `GasAffordRejected`): `gasUsed = 0`, `gasRemaining = originalGasLimit`.
- `Completed` / `RulesRejected` / `ExceptionHandled`: `postExecuteGasSettlement` from `ctx` fields + `feeCtx.m_floorDataGas`.

Remove `txFinalizeGasSettlement` from `OpStackPipelineHookBinder` (ADR-019 Q7: OpStack fee stays in wrapper).

**PR1 gap (closed by PR2):** `refundGas` and gas pool return now live in `settleNormal` / `settleDeposit`; deposit post-pipeline is `finalizeDeposit`.

---

### 2.2 PR2 — settle* facade + deposit + feeCtx narrow (Implemented)

**Spec:** `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md`

#### Interface layering (invariant)

| Layer | Functions | Responsibility |
| --- | --- | --- |
| Sync | `finalizeNormal`, `finalizeDeposit` | Gas/journal math only; **unit-test surface**; must not call `refundGas` or gas pool |
| Async | `settleNormal`, `settleDeposit` | Bridge **only** post-pipeline async entry: `finalize*` → (`refundGas` for normal) → `gasPool.returnGas` |

Bridge must not call `refundGas` directly after PR2.

#### Normal path (PR2 target)

```text
buyGas → runTxPipeline → co_await settleNormal(...)
  ├─ finalizeNormal (sync)
  ├─ refundGas(ctx, feeCtx, settled)
  └─ gasPool.returnGas(settled.gasRemaining, settled.gasUsed)
```

`finalizeNormal` signature narrows to `(ctx, feeCtx, exitKind)` — no `ledger` / `gasPool` params.

`OpStackFeeContext` deletes `m_gasUsed`, `m_gasRemaining`, `m_maxUsedGas`; outputs live only in `OpStackSettlementResult`.

`buyGas` failure: bridge calls `returnGas(gasLimit, 0)` explicitly; delete `GasPoolReturnGuard`.

#### Deposit seam invariants

**Pre-pipeline (bridge only, ordered):**

1. `depositNonce = get_nonce(sender)` — before mint
2. `mint` if `depositTx.mint > 0`
3. `checkpoint()`
4. `runTxPipeline(ctx, hooks)`

**Post-pipeline:** `co_await settleDeposit(...)` → `finalizeDeposit` + `gasPool.returnGas`. No `buyGas` / `refundGas` / L1·operator fee routing.

#### Deposit three-track table (normative)

Rationale: op-geth Regolith+ / `task4-deposit.md`.

| exitKind × evmStatus | gasUsed | journal | sender nonce |
| --- | --- | --- | --- |
| `Completed` + `EVMC_SUCCESS` | actual (`postExecuteGasSettlement`) | `commit()` | +1 |
| `Completed` + non-SUCCESS (e.g. `EVMC_REVERT`) | actual (not gasLimit) | `revert()` if checkpoint | +1 |
| `!= Completed` (entry / intrinsic failure) | `originalGasLimit` | `revert()` if checkpoint | +1 |

Mint is never rolled back on failure.

---

### 3. Phased delivery

| Phase | Scope | Status |
| --- | --- | --- |
| **PR1** | Normal path; `finalizeNormal` gas math; delete shadow fields `m_message`/`m_gasLimit`/`m_state`; module tests | Implemented |
| **PR2** | `settleNormal`/`settleDeposit`; `finalizeDeposit`; gas outputs in `OpStackSettlementResult` only; refund/pool in settlement facade | Implemented |

---

## Consequences

### Positive

- PR1: ctx gas/message single source for normal L2 txs.
- PR2: settlement locality — one async facade per path, table-driven exitKind/deposit tracks.
- Reduces next gas/fee drift class.

### Negative / trade-offs

- `buyGas` / `refundGas` signatures change (OpStack-only).
- PR1 leaves refund/pool in bridge until PR2.
- PR1 leaves deposit inline until PR2.
- Characterization + module tests required before PR2 refactor (G1).

---

## Compliance

### PR1 (reviewers — PR1 merge)

- [x] Normal path: no read of `feeCtx.m_message` / `m_gasLimit` / `m_state`
- [x] No `applySettlement` call sites
- [x] `finalizeNormal` computes gas math from `ctx` after `runTxPipeline`
- [x] `eth/` seam discipline unchanged (ADR-005)

### PR2 (reviewers — PR2 merge)

- [x] `settleNormal` is sole normal post-pipeline async entry (includes `refundGas` + pool)
- [x] `settleDeposit` is sole deposit post-pipeline async entry
- [x] Bridge does not call `refundGas` directly
- [x] Bridge has no `GasPoolReturnGuard`; `buyGas` fail calls `returnGas(gasLimit, 0)`
- [x] `OpStackFeeContext` has no `m_gasUsed` / `m_gasRemaining` / `m_maxUsedGas`
- [x] `finalizeNormal(ctx, feeCtx, exitKind)` — no `ledger` / `gasPool` params
- [x] Deposit pre-pipeline (depositNonce → mint → checkpoint) stays in bridge
- [x] `finalizeDeposit` matches three-track table
- [x] `OpStackDepositSettlementTest` + existing `Deposit*` green
- [x] `OpStackSettleAsyncTest` — async `settleNormal`/`settleDeposit` wiring (14 cases)
- [x] `eth/` seam discipline unchanged (ADR-005)

### Test coverage (settlement layers)

| Layer | Tests |
| --- | --- |
| Sync `finalizeNormal` | `OpStackSettlementTest` |
| Sync `finalizeDeposit` | `OpStackDepositSettlementTest` |
| Async `settleNormal` / `settleDeposit` | `OpStackSettleAsyncTest` |
| Fee ledger routing | `OpStackFeeSettlementCtxTest` |
| E2E oracle | `OpStackSettlementCharacterizationTest`, deposit integration tests |

---

## Appendix A — Fee projection deepening (2026-06-26)

**Status:** Accepted  
**Motivation:** PR1+PR2 made `ctx` the single source for gas/message/state, but normal L2 fee wiring still maintained a **parallel mirror** in `OpStackFeeContext` (`populateFeeContext` copied 15+ fields from `OpStackMessageRequest`). `buyGas` / `refundGas` / `finalizeNormal` read both `ctx` and `feeCtx`. ADR-025 entry-reject abort correctness depends on lifecycle **call-site composition** (`completeNormalTxAfterPipeline`), not a single deep module interface.

**Non-goals:** Deposit path (`settleDeposit`); fee math in `eth/` kernel (ADR-019 Q7); TE `OpStackTransactionExecutorImpl` reshape.

### A.1 Grilling decisions

| # | Question | Choice |
| --- | --- | --- |
| 1 | Deepening boundary | **A** — `OpStackSettlementFacade` (read-only projection) + `OpStackFeeSidecar` (mutable lifecycle state) |
| 2 | Sidecar contents | **A2** — five fields: `effectiveGasPrice`, `baseFee`, `l1CostCharged`, `operatorCostLimit`, `floorDataGas` |
| 3 | View lifecycle | **B3** — view always holds `ctx` + `input` + `sidecar&`; accessors fall back to input when sidecar unset |
| 4 | Fee module shape | **C2** — `OpStackNormalTxFeeCoordinator` deep module: `buyGas` + `completeAfterPipeline` |
| 5 | Deposit | **D1** — unchanged; view shared for precheck only |
| 6 | `OrchestrationProfile::Context` | **E2** — `{ input, view }`; precheck writes via `view.mutableSidecar()` |
| 7 | Test migration | **F3** — keep `finalizeNormal` as internal sync seam; adapter tests on `OpStackFeeSettlement`; ADR-025 tree on `completeAfterPipeline` |
| 8 | Delivery | **Three PRs** — projection → deep module → cleanup |

### A.2 Module layout (target)

| Module | Role |
| --- | --- |
| `OpStackSettlementFacade` | Projection over `TxPipelineContext` + `OpStackMessageRequest` + `OpStackFeeSidecar&`; **no mirrored storage** of request fields |
| `OpStackFeeSidecar` | Lifecycle-mutable fee state only (see A.1 #2) |
| `OpStackNormalTxFeeCoordinator` | Deep module: `buyGas(view)` → pipeline → `completeAfterPipeline(view, …)`; **ADR-025 abort tree internal** |
| `OpStackFeeSettlement` | **Adapter seam** (L1/operator hooks, recipients); not lifecycle's direct interface |
| `finalizeNormal` | **Sync internal seam** (ADR-021 §2.2 invariant preserved); unit-testable gas math |

Delete: `populateFeeContext`. Removed in Appendix A PR3: `OpStackFeeContext`, public `settleNormal`, `completeNormalTxAfterPipeline`.

### A.3 Invariants carried forward from §2

1. **ctx single source** — gas/message/state truth remains `TxPipelineContext`; **no OP fields added to `eth/kernel/state-transition/StateTransitionContext.h`**.
2. **Sync vs async layering** — `finalizeNormal` stays sync, no `refundGas` / gas pool inside it.
3. **Deposit** — `settleDeposit` / three-track table (§2.2) unchanged.
4. **ADR-025** — pre-execution reject (`IntrinsicRejected`, `GasAffordRejected`) must not call `refundGas` / `projectNormalReceiptMeta`; enforced inside `completeAfterPipeline`, not lifecycle call sites.

### A.4 Normal path (post-Appendix A)

```text
view = { ctx, input, sidecar }
session = { input, view }
precheck(session.view) → gasPool → checkpoint
settlement.buyGas(view) → [fail → abort]
runTxPipeline(ctx, …)
settlement.completeAfterPipeline(view, gasPool, feeParams, output)
```

### A.5 Phased delivery

| Phase | Scope | Behavior change |
| --- | --- | --- |
| **PR1** | `OpStackSettlementFacade`, `OpStackFeeSidecar`; Session E2; ledger/precheck read view; delete `populateFeeContext` | None |
| **PR2** | `OpStackNormalTxFeeCoordinator`; lifecycle convergence; ADR-025 tests on `completeAfterPipeline` | Abort/settle locality only (no semantic drift) |
| **PR3** | Remove `OpStackFeeContext`; drop public `settleNormal` / `completeNormalTxAfterPipeline`; doc sync | Implemented |

### A.6 Compliance (Appendix A — reviewers)

**PR1**

- [x] `populateFeeContext` deleted
- [x] No new fields on `TxPipelineContext` under `eth/`
- [x] `OpStackStateTransitionBindings::Context` holds `view`, not `feeCtx`
- [x] All OpStack ctest green; no intentional behavior change

**PR2**

- [x] Lifecycle normal path calls only `buyGas` + `completeAfterPipeline` on settlement module
- [x] ADR-025 characterization matrix green (`OpStackTxLifecycleCharacterizationTest`)
- [x] `finalizeNormal` remains sync-only internal seam
- [x] Bridge/lifecycle does not call `refundGas` or `settleNormal` directly

**PR3**

- [x] `OpStackFeeContext` removed
- [x] Public async helpers removed from `OpStackSettlement.h` (internal to settlement module)
- [x] `OpStackFeeSettlementCtxTest` still covers adapter routing

### A.7 Test surface (Appendix A)

| Layer | Tests |
| --- | --- |
| Sync `finalizeNormal` | `OpStackSettlementTest` (retained) |
| Adapter routing | `OpStackFeeSettlementCtxTest` (retained) |
| Deep module + ADR-025 | `OpStackNormalTxFeeCoordinatorTest` (new), `OpStackTxLifecycleCharacterizationTest` (extended) |
| Async wiring | `OpStackSettleAsyncTest` (via settlement module, not raw `settleNormal`) |
| Deposit | `OpStackDepositSettlementTest`, `Deposit*` (unchanged) |
