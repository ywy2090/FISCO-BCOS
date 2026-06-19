# Task 10 Report: FiscoRevisionConfig + RevisionConfig 纯化

## 完成内容

1. `RevisionConfig` 纯化
   - 在 `bcos-evm/eth/RevisionConfig.h` 删除了 FISCO overlay 字段：
     - `fix_*`
     - `enable_*`
     - `use_raw_address` / `use_web3_timestamp`
   - 保留纯 eth 层所需字段（`revision`、EIP 开关、`calldata_floor_per_token`）。

2. 新增 FISCO overlay 配置
   - 新增 `bcos-evm/bcos/FiscoRevisionConfig.h`：
     - 包含 `ethConfig`（`RevisionConfig`）
     - 包含全部 `fix_*` / `enable_*` / 链身份字段
     - 提供 `eth()` 访问器。

3. 策略返回类型切换
   - `bcos-evm/bcos/FiscoPolicy.h` 的 `computeRevisionConfig()` 改为返回 `FiscoRevisionConfig`。
   - `bcos-evm/eth/vm/EthPolicy.h` 不再写入 FISCO overlay 字段。

4. TE/ExecuteViaHost 接线
   - `ExecuteViaHostInput`、`FiscoExecutionContext` 切换为 `FiscoRevisionConfig`。
   - `executeViaHost()` 中 eth 执行路径改为使用 `revisionConfig.eth()`。
   - `EthHost` 构造已修复为传入：
     - `*input.vm`
     - `input.blockHashes`
     - overlay 的 `fix_storage_status`。
   - `TransactionExecutorImpl` 在 eth 语义处改用 `revisionConfig.eth()`，在 FISCO 语义处继续用 overlay 字段。

5. 预编译门控接线
   - `PrecompiledManager` 的 revision+features 重载改为接收 `FiscoRevisionConfig`。
   - 门控逻辑：
     - 是否启用 gate：`rev.fix_precompiled_feature_gate`
     - eth EIP 开关读取：`rev.eth().eip2537 / rev.eth().eip7212`
     - FISCO feature 继续从 `features` 读取。

6. 测试更新
   - 更新了以下测试以匹配新类型与语义：
     - `TestFiscoPolicy.cpp`
     - `TestStandardEthPolicy.cpp`
     - `ExecuteViaHostCompatTest.cpp`
     - `ExecuteViaHostEip2929Harness.h`
     - `Modexp7823TeTest.cpp`

## 验证结果

1. 构建
   - `rtk cmake --build build --target test-transaction-executor -j$(sysctl -n hw.ncpu)` 通过。

2. 指定测试
   - `rtk ./build/transaction-executor/tests/test-transaction-executor --run_test=FiscoPolicyTest,EthPolicyTest` 通过（7 cases）。

3. bcos-evm tests
   - `rtk ctest --test-dir build/bcos-evm/test --output-on-failure` 通过（13/13）。

4. CI grep 门禁
   - `! rtk grep -r 'bcos-executor' bcos-evm/eth/` 通过（无匹配）。

