# Task 17 Report: EthTxExecutor 接入 + transition 最终收敛

## 实现内容

1. 修改 `bcos-evm/eth/EthTxExecutor.h`
   - 将账户读写从旧 `m_hostContext` 路径切到 rollbackable storage + `m_executionContext.message`：
     - `buyGas()` 使用 `ledger::account::EVMAccount(data.m_rollbackableStorage, msg.sender, false)`
     - `refundGas()` 同步使用上述账户路径
   - `makeReceipt()` 改为消费 `m_executionContext`：
     - 消息来源由 `m_hostContext.message()` 改为 `m_executionContext.message`
     - 日志来源由 `m_hostContext.logs()` 改为 `m_executionContext.logs`
   - 该改动使 `EthTxExecutor` 与 `executeMessage()/executeViaHost` 输出上下文对齐，不再依赖旧 HostContext 数据源。

2. 修改 `bcos-evm/eth/state/transition.cpp`
   - 保持 `transition()` 为薄封装，仅负责：
     - 构造顶层 `evmc_message`
     - 调用 `executeMessage()`
     - 转换 receipt（status/output/gasUsed/gasRefund）
   - 添加注释明确 warm-up/执行/commit-revert 统一在 `executeMessage()` 中处理，避免重复逻辑。

3. warm 路径去重核查
   - `rtk rg "warmTransactionEntry\\(" bcos-evm` 结果显示 warm 入口集中在：
     - `bcos-evm/eth/executeMessage.cpp`
     - `bcos-evm/bcos/FiscoTransactionPrepare.h`
   - `transition.cpp` 与 `bcos/ExecuteViaHost.cpp` 均未重复直接执行 warm 逻辑。

## 回归验证（Step 3 gate）

1. `rtk ctest --test-dir build-c3-3/bcos-evm/test --output-on-failure`
   - 结果：**15/15 PASS**

2. `rtk ctest --test-dir build-c3-3 -R CompatExecuteViaHost --output-on-failure`
   - 结果：**50/50 PASS**

3. `rtk ctest --test-dir build-c3-3 -R "ExecuteViaHostCompat|FIB101" --output-on-failure`
   - 结果：**16/17 PASS**
   - 失败项：
     - `ExecuteViaHostCompat`（聚合套件）
   - 说明：该聚合套件内对应的 6 个 `ExecuteViaHostCompatTest/*` 子用例在同轮回归中均已 PASS，失败表现与历史已记录残留一致（fib88 两个断言在聚合目标上仍走旧行为）。

4. `rtk ctest --test-dir build-c3-3/bcos-evm/test -R OpStackExecuteViaHost --output-on-failure`
   - 结果：**1/1 PASS**
