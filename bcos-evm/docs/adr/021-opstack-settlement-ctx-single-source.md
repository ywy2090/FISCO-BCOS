# ADR-021: OpStack Settlement — ctx Single Source of Truth

**Status:** Implemented (PR1 + PR2)  
**Date:** 2026-06-25 (PR2 section added 2026-06-25)  
**Implementation (PR1):** `OpStackSettlement::finalizeNormal`, `OpStackFeeContext`, `buyGas(ctx, feeCtx)`  
**Spec (PR2):** `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md`  
**Deciders:** bcos-evm architecture  
**Related:** ADR-019, ADR-005, `docs/superpowers/specs/2026-06-25-opstack-settlement-ctx-single-source-design.md`, `bcos-evm/docs/audits/_work/task4-deposit.md`

---

## Context

ADR-019 fixed OpStack intrinsic gas dual-track (`txData.m_message` vs `executeMessage` input). `TxPipelineContext::message` is now the intrinsic debit owner for the shared pipeline.

`OpStackTxExecutionData` duplicated `m_message`, `m_gasLimit`, and `m_state` for `buyGas`, `applySettlement`, and `refundGas`. Settlement read frozen snapshots while the pipeline mutated `ctx.message`. ADR-019 Q14 (`ctx.state` sole owner) was only half satisfied for gas state.

**PR1 delivered** ctx gas/message single-source and sync gas math (`finalizeNormal`). **PR1 did not fully close** settlement locality: `refundGas` and gas pool return remain in `OpStackExecutionBridge.cpp`; deposit post-pipeline remains inline. PR2 completes the deep module.

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
- [x] `eth/` seam discipline unchanged (ADR-005)
