# Task 13 Report: FiscoConstants + bcos-evm 去除 Common.h 常量耦合

## 完成内容

1. 新增 FISCO 常量头，承接原 `bcos-executor/src/Common.h` 常量
   - 新增 `bcos-evm/bcos/FiscoConstants.h`，定义：
     - `EMPTY_EVM_ADDRESS`
     - `EMPTY_EVM_BYTES32`
     - `EMPTY_EVM_UINT256`
     - `USER_APPS_PREFIX`

2. 移除 `FiscoHostExtension` 对 `bcos-executor/src/Common.h` 的依赖
   - 修改 `bcos-evm/bcos/FiscoHostExtension.h`：改为包含 `bcos-evm/bcos/FiscoConstants.h`
   - 修改 `bcos-evm/bcos/FiscoHostExtension.cpp`：`executor::USER_APPS_PREFIX` 全部替换为 `USER_APPS_PREFIX`

3. 移除 `ExecuteViaHost` 对 executor 常量的依赖
   - 修改 `bcos-evm/bcos/ExecuteViaHost.cpp`：
     - 引入 `FiscoConstants.h`
     - `executor::EMPTY_EVM_ADDRESS` 替换为 `EMPTY_EVM_ADDRESS`
     - 去除对 `bcos-executor/Common.h` 的隐式依赖后，补齐本地实现所需表达：
       - 地址空值判断改为 `std::memcmp`
       - `fromEvmC` 调用改为 `state::fromEvmC`

4. 移除 `FiscoPolicy` 对 `bcos-executor/src/Common.h` 的依赖
   - 修改 `bcos-evm/bcos/FiscoPolicy.h`：
     - 改为包含 `FiscoConstants.h`
     - 新增本地 `toFiscoRevision(...)`（按 feature / blockVersion 推导 revision）
     - `executor::EMPTY_EVM_ADDRESS` 替换为 `bcos::evm::EMPTY_EVM_ADDRESS`
     - 地址空值判断改为 `std::memcmp`

5. 同步修复相关测试用例
   - `bcos-evm/test/FiscoHostExtensionTest.cpp`
     - `executor::USER_APPS_PREFIX` 替换为 `USER_APPS_PREFIX`
     - `BlockHashes` 显式改为 `state::BlockHashes`
   - `bcos-evm/test/ExecuteViaHostSmokeTest.cpp`
     - `input.revisionConfig.revision` 更新为 `input.revisionConfig.eth().revision`

## 验证结果

1. `bcos-evm` 目标构建
   - 命令：`rtk cmake --build build-bcos-evm-check --target bcos-evm -j8`
   - 结果：通过

2. 相关测试目标构建与执行
   - 构建命令：
     - `rtk cmake --build build-c3-3 --target FiscoHostExtensionTest EthHostExtensionHooksTest ExecuteViaHostSmokeTest -j8`
   - 执行命令：
     - `rtk test ./build-c3-3/bcos-evm/test/FiscoHostExtensionTest`
     - `rtk test ./build-c3-3/bcos-evm/test/EthHostExtensionHooksTest`
     - `rtk test ./build-c3-3/bcos-evm/test/ExecuteViaHostSmokeTest`
   - 结果：均通过（No errors detected）

3. `grep` 验证
   - 命令：`rtk rg "bcos-executor" bcos-evm`
   - 结果：`FiscoHostExtension / ExecuteViaHost / FiscoPolicy` 相关常量耦合已清理；
     但 `bcos-evm/bcos/Precompiled*.h/.cpp`、`ExecutiveWrapper.h`、`AuthCheck.h` 等仍有既有 `bcos-executor` 路径依赖（本任务未触及该批 precompiled/legacy executive 结构）。
