# Task 3 Report: OpStackFloorGas (EIP-7623)

**Status:** DONE  
**Commit:** `<pending>`  
**Message:** `feat(opstack): add EIP-7623 floorDataGas`

## Summary

Implemented EIP-7623 floor data gas in `bcos-evm-op`:

- **`OpStackFloorGas.h/.cpp`** — `floorDataGas`, `tryFloorDataGas` (with overflow check), `executeEntryFloorDataGasCheck` stub for Task 8 wiring.
- **`OpStackFloorGasTest.cpp`** — op-geth `TestFloorDataGas` vectors + overflow + execute-entry reject/accept cases.

## Formula (op-geth `state_transition.go:120-133`)

```
zeroes = count(data, 0x00)
nonZeroes = len(data) - zeroes
tokens = nonZeroes × 4 + zeroes
floorDataGas = 21000 + tokens × 10
```

Overflow: `(MaxUint64 - TxGas) / TxCostFloorPerToken < tokens` → `GasUintOverflow`.

## TDD

1. Wrote failing `OpStackFloorGasTest` (empty calldata → 21000).
2. Implemented `floorDataGas` / `tryFloorDataGas` with op-geth overflow guard.
3. Added `executeEntryFloorDataGasCheck` stub (gasLimit < floor → `BelowFloor`).
4. All tests pass.

## Test results

```text
cd build/bcos-evm/test && ctest -R OpStackFloorGas --output-on-failure
1/1 Test #19: OpStackFloorGas ..................   Passed

cd build/bcos-evm/test && ctest --output-on-failure
19/19 tests passed (full bcos-evm regression)
```

### Vectors verified

| Case | Expected |
|------|----------|
| Empty calldata | 21_000 |
| 100 zero bytes | 22_000 |
| 100 non-zero bytes | 25_000 |
| 50 zero + 50 non-zero | 23_500 |
| Single zero byte | 21_010 |
| Single non-zero byte | 21_040 |
| tokens > max safe | `GasUintOverflow` |
| gasLimit < floor | `BelowFloor` |
| gasLimit == floor | accept |

## Files changed

| File | Action |
|------|--------|
| `bcos-evm/opstack/OpStackFloorGas.h` | created |
| `bcos-evm/opstack/OpStackFloorGas.cpp` | created |
| `bcos-evm/test/opstack/OpStackFloorGasTest.cpp` | created |
| `bcos-evm/CMakeLists.txt` | add `OpStackFloorGas.cpp` |
| `bcos-evm/test/CMakeLists.txt` | add `OpStackFloorGasTest` target |

## Next

Task 4: State refund counter (`m_gasRefund` in State journal).
