# Task 13 Report: CanTransfer + Isthmus RevisionConfig

## 完成内容

1. 新增 `bcos-evm/eth/Transfer.h`
   - `canTransfer(state, from, value)`：仅余额判定 `balance >= value`。
   - `transfer(state, from, to, value)`：统一余额搬移逻辑。

2. 执行入口 CanTransfer 校验
   - 修改 `OpStackExecuteViaHost.cpp` `executeEntryChecks`。
   - 在非 `skipTransactionChecks` 且 `value != 0` 时执行 `canTransfer`；失败返回 `InsufficientFunds`。

3. 嵌套 CALL 转账校验
   - 修改 `EthHost.cpp` `transferValue`。
   - 使用 `canTransfer` + `transfer`，保持与 execute-entry 规则一致。

4. RevisionConfig 预设
   - 修改 `bcos-evm/eth/RevisionConfig.h`：
     - 新增 `prague_post_execution` flag（默认 false）。
     - 新增 `makeIsthmusRevisionConfig()`，显式设定 `prague_post_execution=false`。

5. 测试
   - 新增 `CanTransferTest.cpp`（不足余额拒绝、predeploy 转账不走 deny-list）。
   - 新增 `IsthmusPostExecutionPolicyTest.cpp`。
   - 更新 `bcos-evm/test/CMakeLists.txt` 注册。

## TDD 与验证

- RED：`IsthmusPostExecutionPolicyTest` 先因缺少 `makeIsthmusRevisionConfig` 编译失败。
- GREEN：补齐实现后通过：
  - `ctest -R 'CanTransfer|IsthmusPostExecutionPolicy'` 2/2 通过。
- 全量：
  - `ctest --test-dir BUILD/bcos-evm/test --output-on-failure` 34/34 通过（Task 13 时点）。
