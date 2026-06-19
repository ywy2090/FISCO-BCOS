## Task 7 Report — OpStackExecuteViaHost wiring (Stage A)

### Status
DONE

### Delivered
- Updated `bcos-evm/opstack/OpStackExecuteViaHostInput` to use EIP-1559 inputs (`gasTipCap`/`gasFeeCap`) and removed legacy `gasPrice`/`hasGasFeeCap` from the input API.
- Wired non-deposit execute path in `bcos-evm/opstack/OpStackExecuteViaHost.cpp`:
  - `loadOpStackFeeParams(state)`
  - `buyGas`
  - `executeEntryChecks` (floor gas check via `executeEntryFloorDataGasCheck`)
  - `executeMessage`
  - `postExecuteGasSettlement`
  - `refundGas`
- Switched execution gas price feed to `effectiveGasPrice = min(gasTipCap + baseFee, gasFeeCap)` for EVM context.
- Wired real fee formulas (`l1CostFjord`, `operatorCostIsthmus`) from loaded L1Block fee params; removed smoke-test `m_l1CostFunc` mock injection.
- Added `bcos-evm/test/opstack/OpStackSettlementTest.cpp`:
  - 4-way routing assertions (coinbase tip, base fee recipient `0x19`, L1 recipient `0x1A`, operator recipient `0x1B`)
  - hard-failure path still refunds unused gas while retaining/routing OP fees.
- Added `EvmoneParity_noDoubleCount` case to `bcos-evm/test/opstack/CalcRefundTest.cpp` (Q10-A gate).
- Updated `bcos-evm/test/CMakeLists.txt` to register `OpStackSettlement` test target.

### Verification
```bash
rtk cmake --build build --target bcos-evm-op OpStackExecuteViaHostSmokeTest OpStackSettlementTest CalcRefundTest OpStackFloorGasTest OpStackFeeTest RollupCostTest RefundIsthmusTest -j8
rtk ctest --test-dir build/bcos-evm/test -R 'RollupCost|OpStackFee|RefundIsthmus|CalcRefund|OpStackFloorGas|OpStackSettlement|OpStackExecuteViaHost' --output-on-failure
rtk ctest --test-dir build/bcos-evm/test --output-on-failure
```

### CTest Summary
- Stage A gate subset: **7/7 passed** (`RollupCost`, `OpStackFee`, `RefundIsthmus`, `CalcRefund`, `OpStackFloorGas`, `OpStackSettlement`, `OpStackExecuteViaHost`)
- Full `bcos-evm` ctest: **24/24 passed**
