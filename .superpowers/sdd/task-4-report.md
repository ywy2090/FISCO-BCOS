# Task 4 Report: EthTxFinalize gas metering (TDD)

## Status

**COMPLETE**

## Commit

- `b88085a73` — feat(eth): extract EthTxFinalize gas metering from TE

## Changes

### Extended `bcos-evm/eth/settlement/EthTxFinalize.h`

- `EthTxFinalizeResult { int64_t gasUsed; uint64_t gasRemaining; }` (from Task 3 stub)
- `bool isEthPreExecutionReject(StateTransitionExitKind) noexcept`
- `void abortEthAfterBuyGas(StateTransitionContext&, EthMessageResult&, int64_t originalGasLimit)`
- `EthTxFinalizeResult finalizeEthNormal(...)` — **no `isWeb3`**; EIP-7623 gate: `ctx.revisionConfig.eip7623 && snapshot.gasLimit > 0`

### Created `bcos-evm/eth/settlement/EthTxFinalize.cpp`

- Logic moved from `EthTransactionExecutorImpl::settleGasUsedFromEvmResult`
- Pre-execution reject → `gasUsed=0`, full limit as `gasRemaining`
- Completed/ExceptionHandled → `settleTopLevelTransactionGas` (7623 path) or `originalGasLimit - gas_left` (legacy)
- `abortEthAfterBuyGas` → revert checkpoint, `gasUsed=0`, `build_diff()`
- `topLevelIncludedTxVmError` retained but unused (ADR-015 GAP-TE-002 placeholder)

### Modified `bcos-evm/test/eth/EthFeeSettlementStateTest.cpp`

- Added `finalizeEthNormal_eip7623_uses_settlement_snapshot`

## TDD

| Step | Result |
|------|--------|
| Write failing test | Expected link error (not run before impl; build after test add would fail) |
| Implement EthTxFinalize.cpp + header declarations | Build OK |
| Run EthFeeSettlementStateTest | 3/3 PASS |

## Test Summary

```text
cd build-bcos-evm-check && cmake --build . --target EthFeeSettlementStateTest
./bcos-evm/test/EthFeeSettlementStateTest
Running 3 test cases...
*** No errors detected
```

Exit code: 0

## Self-Review

| Check | Result |
|-------|--------|
| ADR-005: no `isWeb3` in eth layer; 7623 gate uses `eip7623 && snapshot.gasLimit > 0` | PASS |
| Pre-exec reject: `IntrinsicRejected \| GasAffordRejected` → zero gasUsed | PASS |
| `abortEthAfterBuyGas`: revert + gasUsed=0 + stateDiff | PASS |
| CMake GLOB picks up `EthTxFinalize.cpp` (no lib CMake change) | PASS |
| Did not touch `DeductIntrinsicGas.h` | PASS |

## Concerns

1. **TE not wired yet** — `EthTransactionExecutorImpl::settleGasUsedFromEvmResult` still uses `isWeb3 && eip7623` gate; coordinator wiring (later task) will switch to `finalizeEthNormal` and drop TE duplicate logic.
2. **Behavior delta on 7623 gate** — Eth layer now activates 7623 settlement whenever `eip7623 && snapshot.gasLimit > 0` (no tx-type check). This matches ADR-005 intent; TE still has legacy `isWeb3` guard until migrated.
3. **`topLevelIncludedTxVmError` unused** — Same as current TE; ADR-015 peak-gas fix deferred to coordinator integration.
4. **No characterization tests for abort/pre-exec paths** — Brief only required EIP-7623 snapshot test; additional cases can land with coordinator wiring.

## Next steps (out of scope)

- Wire `finalizeEthNormal` / `abortEthAfterBuyGas` into `applyEthMessage` coordinator
- Remove `EthTransactionExecutorImpl::settleGasUsedFromEvmResult` duplicate
- Add characterization tests for pre-exec reject and abort paths if needed

## Review Fix: gasRemaining semantics

### Problem

`finalizeEthNormal` set `gasRemaining = evmcResult.gas_left`, but `EthFeeSettlement::refundGas` passes `settled.gasRemaining` to `planPostExecution`, which expects post-settlement unused gas (`gasLimit - gasUsed`). TE uses `gasLimit - gasUsed`, not raw `gas_left`.

### Fix

- `EthTxFinalize.cpp`: after computing `gasUsed`, set `gasRemaining = max(0, originalGasLimit - gasUsed)`; removed raw `gas_left` assignment.
- `EthFeeSettlementStateTest.cpp`: strengthened `finalizeEthNormal_eip7623_uses_settlement_snapshot` — assert `gasUsed` matches `settleTopLevelTransactionGas` oracle (1000 for floor-uplift fixture) and `gasRemaining == 0`.

### Verification

```text
cmake --build build-bcos-evm-check --target EthFeeSettlementStateTest bcos-evm-eth
./build-bcos-evm-check/bcos-evm/test/EthFeeSettlementStateTest
Running 3 test cases...
*** No errors detected
```

Commit: `b9b754955` — fix(eth): align EthTxFinalize gasRemaining with refund contract (new commit on top of `b88085a73`; amend of original blocked by pre-commit format hook)
