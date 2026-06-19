## Task 6 Report — OpStackGasSettlement + OpStackTxExecutor rewrite

### Status
DONE

### Delivered
- Added `bcos-evm/opstack/OpStackGasSettlement.h` with
  `postExecuteGasSettlement(gasLimit, gasLeft, stateRefund, floorDataGas)`.
- Rewrote `bcos-evm/opstack/OpStackTxExecutor` into declaration+implementation (`.h` + `.cpp`).
- Implemented `resolveEffectiveGasPrice = min(gasTipCap + baseFee, gasFeeCap)`.
- Implemented dual-track `buyGas`:
  - charge path (`mgval`) uses `effectiveGasPrice`;
  - balance check uses `gasFeeCap` for EIP-1559-style path.
- Implemented `refundGas` routing to:
  - coinbase (tip),
  - `0x4200...0019` (base fee),
  - `0x4200...001A` (L1 fee),
  - `0x4200...001B` (operator fee),
  plus sender gas return.
- Implemented `refundIsthmusOperatorCost` with `limitCost - usedCost` refund to sender.
- Removed hard-failure early-return behavior in `OpStackTxExecutor` path; settlement+refund path always runs for non-deposit txs.
- Settlement source switched to `State::get_refund()` (not `evmc_result.gas_refund`).

### Tests (TDD)
- Added `bcos-evm/test/opstack/CalcRefundTest.cpp` (pure settlement formula checks).
- Added `bcos-evm/test/opstack/RefundIsthmusTest.cpp` (`gasLimit=1618`, `gasUsed=500`, delta refund).
- Updated `bcos-evm/test/CMakeLists.txt` for both tests.

### Verification
```bash
rtk cmake -S . -B build
rtk cmake --build build --target bcos-evm-op CalcRefundTest RefundIsthmusTest -j8
rtk ctest --test-dir build/bcos-evm/test -R "CalcRefund|RefundIsthmus" --output-on-failure
rtk ctest --test-dir build/bcos-evm/test --output-on-failure
```

Result: full `bcos-evm` ctest passed (`23/23`).
