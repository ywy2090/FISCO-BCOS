# ADR-021: OpStack Settlement — ctx Single Source of Truth

**Status:** Implemented (PR1)  
**Date:** 2026-06-25  
**Implementation:** `OpStackSettlement::{finalizeNormal}`, `OpStackFeeContext`, `buyGas(ctx, feeCtx)`  
**Deciders:** bcos-evm architecture  
**Related:** ADR-019, ADR-005, `docs/superpowers/specs/2026-06-25-opstack-settlement-ctx-single-source-design.md`

---

## Context

ADR-019 fixed OpStack intrinsic gas dual-track (`txData.m_message` vs `executeMessage` input). `TxPipelineContext::message` is now the intrinsic debit owner for the shared pipeline.

`OpStackTxExecutionData` still duplicates `m_message`, `m_gasLimit`, and `m_state` for `buyGas`, `applySettlement`, and `refundGas`. Settlement reads frozen snapshots while the pipeline mutates `ctx.message`. ADR-019 Q14 (`ctx.state` sole owner) is only half satisfied for gas state.

---

## Decision

### 1. Gas/message truth source (normal L2 txs)

| Concern | Single source |
| --- | --- |
| Message (sender, value, post-intrinsic gas) | `TxPipelineContext::message` |
| Original tx gas budget | `TxPipelineContext::originalGasLimit` |
| EVM outcome | `TxPipelineContext::evmcResult` |
| State mutations | `TxPipelineContext::state` |

`OpStackFeeContext` (narrowed `OpStackTxExecutionData`) holds **fee ledger state only** — no `m_message`, `m_gasLimit`, `m_state`.

### 2. OpStackSettlement deep module

Introduce `OpStackSettlement::finalizeNormal(ctx, feeCtx, exitKind, ledger, gasPoolHooks)` as the **only** normal-path settlement call site after `runTxPipeline`.

- Early-exit (`IntrinsicRejected`, `GasAffordRejected`): `gasUsed = 0`, full `buyGas` refund.
- `Completed`: `postExecuteGasSettlement` from `ctx` fields, then `refundGas`.

Remove `txFinalizeGasSettlement` from `OpStackPipelineHookBinder` (ADR-019 Q7: OpStack fee stays in wrapper).

### 3. Phased delivery

| Phase | Scope |
| --- | --- |
| **PR1** | Normal path only; `finalizeNormal`; delete shadow fields; tests |
| **PR2** | `finalizeDeposit`; `gasUsed` as return value; further narrow `OpStackFeeContext` |

Deposit branch remains inline in `OpStackExecutionBridge.cpp` until PR2.

---

## Consequences

### Positive

- Completes ADR-019 Q14 intent for OpStack gas state.
- Settlement locality: one module, one call site, table-driven exitKind.
- Reduces next gas/fee drift class.

### Negative / trade-offs

- `buyGas` / `refundGas` signatures change (OpStack-only).
- PR1 leaves deposit on legacy `txData` shape temporarily.
- Characterization tests required before refactor to lock early-exit behavior.

---

## Compliance (PR reviewers)

- [x] Normal path: no read of `feeCtx.m_message` / `m_gasLimit` / `m_state`
- [x] No `applySettlement` call sites
- [x] `finalizeNormal` is sole normal settlement entry
- [x] `eth/` seam discipline unchanged (ADR-005)
