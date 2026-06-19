# Task 15 Report: Receipt Metadata + Final Regression Gate

## 完成内容

1. 新增 receipt 元数据结构
   - 新增 `bcos-evm/opstack/OpStackReceiptMeta.h`：
     - `l1Fee`
     - `operatorFee`
     - `depositNonce`

2. 输出结构与执行路径接线
   - `OpStackExecuteViaHostOutput` 增加 `receiptMeta` 字段。
   - `OpStackExecuteViaHost.cpp`：
     - deposit 路径记录 `depositNonce`。
     - non-deposit 路径回填 `l1Fee` / `operatorFee`。

3. Smoke 完整化
   - 更新 `OpStackExecuteViaHostSmokeTest.cpp`：
     - 覆盖 coinbase tip、baseFee recipient(`0x19`)、operator fee recipient(`0x1B`)。
     - 校验 `receiptMeta.l1Fee`、`receiptMeta.operatorFee`。
     - 新增 hard-failure case（INVALID opcode）验证仍返还剩余 gas 且继续路由费用。

4. Spec 状态
   - 更新 `docs/superpowers/specs/2026-06-18-opstack-isthmus-design.md`
   - 顶部状态改为 `Implemented (Stage A + Stage B)`。

## 回归与 Stage B Gate

1. Task 15 全量回归：
   - `ctest --test-dir BUILD/bcos-evm/test --output-on-failure`
   - 结果：38/38 通过。

2. Stage B merge gate（Task 15 要求）：
   - `ctest --test-dir BUILD/bcos-evm/test -R 'RollupCost|OpStackFee|RefundIsthmus|CalcRefund|OpStackFloorGas|OpStackSettlement|OpStackExecuteViaHost|DepositTxPreCheck|DepositMint|DepositNoFeeRouting|L1AttributesDeposit|L1AttributesDepositFailure|GasFeeCapBalance|BlobGasBalance|L1BlockGetter|Eip7702PreCheck|Eip7702ApplyAuthorization|CanTransfer|IsthmusPostExecutionPolicy' --output-on-failure`
   - 结果：19/19 通过。
