# Task 14 Report: L1 Attributes Deposit E2E + Stage B Tests

## 完成内容

1. 新增测试辅助
   - `bcos-evm/test/helpers/ApplyStateDiffToView.h`
   - 用于按交易顺序将上一笔 `stateDiff` 回灌到 `InMemoryStateView`，模拟区块内顺序执行。

2. 完成 `L1AttributesDepositTest`
   - 将 skeleton 改为完整 E2E：
     - 先执行 L1 attributes deposit（fixture: `isthmus_l1_attributes.bin`）。
     - 应用 `stateDiff` 到 view。
     - 再执行 user tx，验证 L1 fee recipient 实际被计费（`> 0`）。

3. 新增 Stage B 关键测试
   - `L1AttributesDepositFailureTest.cpp`
   - `GasFeeCapBalanceTest.cpp`
   - `BlobGasBalanceTest.cpp`
   - `L1BlockGetterTest.cpp`

4. CMake 注册
   - 更新 `bcos-evm/test/CMakeLists.txt`，新增以上 test target。
   - 为 `L1AttributesDepositTest` / `L1AttributesDepositFailureTest` 增加 `OPSTACK_FIXTURES_DIR` 编译定义。

## TDD 与验证

- RED：`L1AttributesDepositTest` 初版因余额不足触发 buyGas 失败。
- GREEN：提高 sender 余额后通过。
- 任务测试集：
  - `ctest -R 'L1AttributesDeposit|L1AttributesDepositFailure|GasFeeCapBalance|BlobGasBalance|L1BlockGetter'` 5/5 通过。
- 全量：
  - `ctest --test-dir BUILD/bcos-evm/test --output-on-failure` 38/38 通过（Task 14 时点）。
