# ADR-025: OpStack Normal Entry Failure Early Return (R3-7623-1)

**Status:** Accepted  
**Date:** 2026-06-26  
**Related:** ADR-021, ADR-023, Wave 3 audit **R3-7623-1**  
**op-geth anchor:** `state_transition.go` `innerExecute` intrinsic/floor/transfer errors return before `returnGas` / fee routing

---

## Context

Wave 3 audit **R3-7623-1** noted a split between orchestration and TE for **normal L2** txs that fail **before EVM execution**:

| Layer | Previous behavior |
| --- | --- |
| `runOpStackTxLifecycle` | `buyGas` → pipeline `IntrinsicRejected` / `GasAffordRejected` → **`settleNormal` + `refundGas`** |
| TE `OpStackTransactionExecutorImpl` | `applyStateDiff` only on `EVMC_SUCCESS` / `EVMC_REVERT` |

Ephemeral `refundGas` produced **phantom** `receiptMeta.l1Fee` / fee routing in `stateDiff` that never landed on chain, while op-geth treats these as **hard failures** before settlement (`execute()` error, tx not included).

Deposit entry failures are **out of scope** — they intentionally use `settleDeposit` + `gasUsed = gasLimit` (Regolith+), per ADR-021.

---

## Decision

### 1. Normal path checkpoint around `buyGas`

After `acquireGasPool`, normal L2 txs take a **state checkpoint** before `buyGas`.

### 2. Pre-execution reject abort (scheme B)

When `runTxPipeline` exits with:

- `TxPipelineExitKind::IntrinsicRejected`, or
- `TxPipelineExitKind::GasAffordRejected`

lifecycle **must not** call `settleNormal` / `refundGas` / `projectNormalReceiptMeta`.

Instead:

1. `state.revert()` (undo `buyGas` debit)
2. `releaseGasPoolFullLimit(limit, used=0)`
3. `output.gasUsed = 0`
4. empty / unchanged `stateDiff`
5. no `receiptMeta.l1Fee` / `operatorFee`

`buyGas` failure uses the same abort helper (revert + gas pool release).

### 3. Successful / post-EVM paths unchanged

On `Completed`, `RulesRejected`, or `ExceptionHandled`:

1. `state.commit()` after pipeline (persists `buyGas` debit)
2. `settleNormal` → `refundGas` → `projectNormalReceiptMeta` as before

### 4. TE receipt contract (documented, not changed here)

TE continues to:

- apply `stateDiff` only on `EVMC_SUCCESS` / `EVMC_REVERT`
- always call `makeReceipt()` in Finalize

After this ADR, entry-failure outputs have **`gasUsed = 0`**, **no fee meta**, and **empty `stateDiff`**, so receipt fields align with committed state even though inclusion semantics may still differ from op-geth (FISCO may include failed txs with status ≠ success).

---

## Characterization oracles (ADR-023 matrix update)

| # | Scenario | Key assertions |
| --- | --- | --- |
| 2 | Normal `IntrinsicRejected` | `gasUsed == 0`; gasPool `returnGas(fullLimit, 0)`; **sender balance unchanged**; **no `receiptMeta.l1Fee`** |
| 8 | Normal `GasAffordRejected` | same abort contract as #2 |
| 3 | Normal `buyGas` fail | unchanged — abort before pipeline |

Tests: `OpStackTxLifecycleCharacterizationTest` cases `lifecycle_normal_intrinsic_reject_*`, `lifecycle_normal_gas_afford_reject_*`, `lifecycle_normal_buy_gas_fail_*`.

---

## Consequences

### Positive

- Closes R3-7623-1 orchestration half: no phantom fee routing on entry failure.
- `stateDiff` / `receiptMeta` consistent for TE gate.
- `settleNormal` / `finalizeNormal` early-exit math unchanged (still unit-tested in isolation).

### Trade-offs

- op-geth still **excludes** invalid txs from the block; FISCO TE may still **include** them with failed status — documented, not solved in this ADR.
- `RulesRejected` / `ExceptionHandled` still route through `settleNormal` (OpStack `checkTransactionRules` is no-op today).

---

## Compliance

- [x] `OpStackTxLifecycle.cpp` abort on `IntrinsicRejected` / `GasAffordRejected`
- [x] Characterization tests #2, #8, #3 green
- [x] Deposit paths unchanged
