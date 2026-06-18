# Task C5-1 Report

## Scope
- `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`
  - 将执行上下文从已删除的 `hostcontext::ExecuteFrame` 迁移为 `FiscoExecutionContext`。
  - 移除旧的 transient storage / warmset access 字段与 `vm/ExecuteFrame.h` 依赖。
  - `Prepare` 阶段改为基于 `FiscoStateView + state::State + prepareTransaction()` 进行交易预热。
  - `Execute` 阶段改为构建 `ExecuteViaHostInput` 并调用 `executeViaHost()`。
  - 成功执行后接线 `state::applyStateDiff()` 写回 `m_rollbackableStorage`。
  - 按 `fix_gas_precheck` 分支保留气费结算顺序：`refundGas()` / `consumeBalance()`。
  - `settleGasUsedFromEvmResult()` 改从 `m_executionContext.gasSettlementSnapshot` 读取快照。
  - 接入 `authChecker` / `precompileCaller` / `createAuthTableInvoker`。
- `bcos-evm/bcos/FiscoTxExecutor.h`
  - 将 `data.m_hostContext.*` 全量替换为 `data.m_executionContext.*`。
  - 删除 `getAccount(...)` 依赖，改为直接构造
    `ledger::account::EVMAccount(data.m_rollbackableStorage, address, use_raw_address)`。
  - `makeReceipt()` 使用 `m_executionContext.logs` 和 `fix_revert_logs` 语义。

## Verification

### 1) 构建验证（请求中的兼容目标）
Command:

`rtk cmake --build build --target bcos-evm test-execute-via-host-compat -j8`

Result:
- PASS（`Built target bcos-evm`，`Built target test-execute-via-host-compat`）

### 2) bcos-evm 测试集（请求中的 8/8）
Command:

`rtk ctest --test-dir build/bcos-evm/test`

Result:
- PASS（`100% tests passed, 0 tests failed out of 8`）

### 3) ExecuteViaHost 兼容测试
Command:

`rtk ctest --test-dir build/transaction-executor/tests -R "^ExecuteViaHostCompat$"`

Result:
- PASS（`1/1 Test #215: ExecuteViaHostCompat ... Passed`）

## Known Blockers (pre-existing, out of C5-1 scope)
- `rtk cmake --build build --target transaction-executor -j8` 仍失败于 `bcos-executor` 现存不兼容：
  - `TransactionExecutive.cpp` 对 `VMFactory::create()` 参数个数不匹配
  - `Eip2929Util::eip2929Enabled(...)` 调用签名不匹配
  - `TransactionExecutor` 与 `VMFactory` 类型命名空间不一致导致构造签名不匹配
- 这些错误发生在 `bcos-executor` 路径，且在本任务改动之前已存在，不阻塞本任务要求的 `bcos-evm` 与 `ExecuteViaHostCompat` 验证。
