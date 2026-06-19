# Task 2 Report: OpStackFee (Fjord L1 + Isthmus operator)

**Status:** DONE  
**Commit:** _(see git log)_  
**Message:** `feat(opstack): Fjord L1 and Isthmus operator fee formulas`

## Summary

Implemented OpStack fee formulas in `bcos-evm-op`:

- **`OpStackFee.h/.cpp`** — `OpStackFeeParams`, `l1CostFjord`, `operatorCostIsthmus`, `loadOpStackFeeParams`.
- **`empty_tx.bin`** — op-geth `emptyTx.MarshalBinary()` fixture (`fastLz=31`).
- **`OpStackFeeTest.cpp`** — op-geth `rollup_cost_test.go` vectors + slot 3/8 unpack test.

## TDD

1. Wrote failing `OpStackFeeTest` (Fjord empty tx → 3_203_000; operator gas=1618 → fixture value).
2. Implemented §6.1 Fjord L1 formula and §6.2 Isthmus operator formula; slot unpack mirrors op-geth `ExtractEcotoneFeeParams` / `ExtractOperatorFeeParams`.
3. Fixed signed intercept handling (`L1_COST_INTERCEPT` negative) via `s256` before clamp to `MIN_TX_SIZE_SCALED`.
4. All tests pass.

## Test results

```text
cd build/bcos-evm/test && ctest -R OpStackFee --output-on-failure
1/1 Test #18: OpStackFee .......................   Passed

cd build/bcos-evm/test && ctest --output-on-failure
18/18 tests passed (full bcos-evm regression)
```

### Vectors verified

| Case | Expected |
|------|----------|
| `empty_tx.bin` fastLz | 31 |
| Fjord L1 cost (test params) | 3_203_000 |
| `operatorCostIsthmus(1618)` | 1256417826611659930 |
| Empty `RollupCostData` | L1 cost 0 |
| Zero operator params | operator cost 0 |
| `loadOpStackFeeParams` slots 1/3/7/8 | matches test constants |

## Files changed

| File | Action |
|------|--------|
| `bcos-evm/opstack/OpStackFee.h` | created |
| `bcos-evm/opstack/OpStackFee.cpp` | created |
| `bcos-evm/test/fixtures/opstack/empty_tx.bin` | created |
| `bcos-evm/test/opstack/OpStackFeeTest.cpp` | created |
| `bcos-evm/CMakeLists.txt` | add `OpStackFee.cpp` |
| `bcos-evm/test/CMakeLists.txt` | add `OpStackFeeTest` target |

## Notes

- Fjord formula: `estimatedSizeScaled = max(MIN_TX_SIZE_SCALED, INTERCEPT + FASTLZ_COEF × fastLzSize)`; `l1Cost = estimatedSizeScaled × l1FeeScaled / FJORD_DIVISOR`.
- Operator formula: `gas × operatorFeeScalar / 1_000_000 + operatorFeeConstant`; all-zero slot 8 → 0.
- Slot 3 scalars at bytes `[16:20)` / `[20:24)`; slot 8 at `[20:24)` / `[24:32)`.

## Next

Task 3: wire `OpStackFee` into `OpStackTxExecutor` buyGas / refundGas settlement.
