# Task 16 Report: OpHostExtension + OpStackExecuteViaHost

## 实现内容

1. 新增 `bcos-evm/opstack/OpHostExtension.h`
   - `OpHostExtension : state::HostExtension`
   - `prepareMessage()` 为 no-op
   - `tryChainPrecompile()` 预留 OP L1Block 预编译地址 `0x4200000000000000000000000000000000000015` stub 分支

2. 新增 `bcos-evm/opstack/OpStackExecuteViaHost.h/.cpp`
   - 新增 `OpStackExecuteViaHostInput/Output`
   - 新增 `opStackExecuteViaHost()`：
     - 构建 `state::State`
     - 执行 `OpStackTxExecutor::buyGas()`
     - 调用 `executeMessage()`（注入 `OpHostExtension`）
     - 计算 `gasUsed`
     - 执行 `OpStackTxExecutor::refundGas()`
     - 返回 `stateDiff/logs/evmcResult`

3. 修改 `bcos-evm/opstack/OpStackTxExecutor.h`
   - 移除 `m_hostContext` 路径依赖
   - 改为 `OpStackTxExecutionData`（`state::State* + evmc_message`）
   - `buyGas/refundGas` 改为直接读写 `state::State` 余额

4. 新增测试 `bcos-evm/test/opstack/OpStackExecuteViaHostSmokeTest.cpp`（3 cases）
   - `l1_fee_recipient_gets_fee_on_success`
   - `insufficient_balance_fails_before_execution`
   - `revert_refunds_unused_gas_and_keeps_l1_fee`

5. CMake 接线
   - `bcos-evm/CMakeLists.txt` 增加 `opstack/OpStackExecuteViaHost.cpp`
   - `bcos-evm/test/CMakeLists.txt` 增加 `OpStackExecuteViaHostSmokeTest` target 与 `OpStackExecuteViaHost` ctest

## 验证命令与结果

1. 重新配置
   - 命令：`rtk cmake -S . -B build-c3-3`
   - 结果：PASS

2. 构建 OpStack 测试目标
   - 命令：`rtk cmake --build build-c3-3 --target OpStackExecuteViaHostSmokeTest -j$(sysctl -n hw.ncpu)`
   - 结果：PASS

3. 运行新增测试
   - 命令：`rtk ctest --test-dir build-c3-3/bcos-evm/test -R OpStackExecuteViaHost --output-on-failure`
   - 结果：**1/1 PASS**

4. 运行 `bcos-evm/test` 全量
   - 命令：`rtk ctest --test-dir build-c3-3/bcos-evm/test --output-on-failure`
   - 结果：**15/15 PASS**
