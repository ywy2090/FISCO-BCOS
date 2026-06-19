# Task 11 Report: eth::executeMessage() 抽取

## 完成内容

1. 新增 eth 执行入口抽象
   - 新增 `bcos-evm/eth/executeMessage.h`：
     - `ExecuteMessageInput`
     - `ExecuteMessageOutput`
     - `executeMessage(ExecuteMessageInput input)`
   - 新增 `bcos-evm/eth/executeMessage.cpp`：
     - 抽取并承接原 `ExecuteViaHost` 中的 warm + `EthHost` + 顶层 `vm.execute` + commit/revert 主流程。
     - 支持传入 `StateView`（普通视图）或已构造的 `state::State`（复用已有变更上下文）。
     - 输出 `evmc::Result`、`StateDiff`、`logs`。

2. ExecuteViaHost 接线改造
   - `bcos-evm/bcos/ExecuteViaHost.cpp`：
     - 引入 `executeMessage` 调用，替换原本内联的 warm/host/vm.execute/commit/revert 逻辑。
     - 保留 FISCO 层特有逻辑在 `ExecuteViaHost`：
       - auth check
       - EIP-7623 calldata/intrinsic gas 预处理
       - FISCO value transfer 策略
       - `NotFoundCodeError` 分支与错误码映射
     - 结果映射调整为：
       - `executeMessage` 返回 `evmc::Result`，由 `adoptResult()` 转为 `EVMCResult`。
       - `executeMessage` 返回的 `state::LogEntry` 继续在 `ExecuteViaHost` 转成 `protocol::LogEntry`。

3. 构建系统接入
   - `bcos-evm/CMakeLists.txt` 增加 `eth/executeMessage.cpp` 编译单元。

4. 新增 smoke test（TDD）
   - 新增 `bcos-evm/test/ExecuteMessageSmokeTest.cpp`：
     - 使用 `InMemoryStateView` 构造简单 `CALL` 场景。
     - 断言 `executeMessage()` 返回 `EVMC_SUCCESS` 且日志为空。
   - `bcos-evm/test/CMakeLists.txt` 新增 `ExecuteMessageSmoke` 测试目标。

## 验证结果

1. RED（先失败）
   - `rtk cmake --build build --target ExecuteMessageSmokeTest -j$(sysctl -n hw.ncpu)`
   - 初次失败：`bcos-evm/eth/executeMessage.h` 不存在（符合测试先行）。

2. GREEN（实现后通过）
   - `rtk cmake --build build --target ExecuteMessageSmokeTest -j$(sysctl -n hw.ncpu)` 通过。

3. 指定测试集
   - `rtk ctest --test-dir build/bcos-evm/test --output-on-failure` 通过（14/14）。

4. transaction-executor policy tests
   - 本任务未修改 `test-transaction-executor` policy 相关文件，按“if touched”条件未额外执行该组测试。
