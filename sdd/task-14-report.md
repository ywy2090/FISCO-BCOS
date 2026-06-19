# Task 14 Report: ExecutiveWrapper 上移 + 删除 TE externalCaller + CompatHostShim 走 EthHost::call

## 完成内容

1. `ExecutiveWrapper` 从 `bcos-evm` 上移到 `transaction-executor`
   - 新增：`transaction-executor/bcos-transaction-executor/ExecutiveWrapper.h`
   - 删除：`bcos-evm/bcos/ExecutiveWrapper.h`
   - 更新引用：
     - `bcos-evm/bcos/AuthCheck.h`
     - `bcos-evm/bcos/PrecompiledImpl.h`
     - `transaction-executor/bcos-transaction-executor/precompiled/ExecutiveWrapper.h`

2. 删除 `TransactionExecutorImpl` 的 stub `externalCaller`
   - 修改 `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`：
     - 移除局部 `externalCaller = "external call not available"` stub 变量
     - `input.authChecker` 改为直接调用精简后的 `checkAuth(...)`（不再传 externalCaller）
     - `precompileCaller` / `createAuthTableInvoker` 在需要 legacy executive 的地方直接使用 `noOpExternalCaller()`

3. 精简 `checkAuth` externalCaller 参数链路
   - 修改 `bcos-evm/bcos/AuthCheck.h`：
     - 新增 `noOpExternalCaller()`
     - `checkAuth(...)` 不再接收 `externalCaller` 参数
   - 修改 `bcos-evm/bcos/FiscoPolicy.h`：
     - `FiscoPolicy::checkAuth(...)` 同步移除 `externalCaller` 形参并透传到新签名

4. `CompatHostShim` 改为走真实 `EthHost::call()` 递归
   - 修改 `transaction-executor/tests/ExecuteViaHostEip2929Harness.h`：
     - `execute()` / `externalCall()` 不再走 `runEvm()` 旁路
     - 统一通过 `host().call(msg)` 执行
     - 删除 `runEvm` 中手动 checkpoint/vm.execute/commit/revert 逻辑
     - `externalCall()` 保留 `deriveMessage(... seq + 1 ...)` 以保持 CREATE/CREATE2 地址推导兼容

## 验证结果

1. 构建 transaction-executor 测试目标
   - `rtk cmake --build build-c3-3 --target test-transaction-executor -j8`
   - 结果：通过

2. `CompatExecuteViaHost` 回归（50/50）
   - `rtk ctest --test-dir build-c3-3 -R "CompatExecuteViaHost" --output-on-failure`
   - 结果：**50/50 PASS**

3. `ExecuteViaHostCompat|FIB101` 回归
   - 先构建 scheduler 测试目标（解决 FIB101 可执行文件缺失）：
     - `rtk cmake --build build-c3-3 --target test-transaction-scheduler -j8`
   - 执行（按 suite 名精确匹配）：
     - `rtk ctest --test-dir build-c3-3 -R "^ExecuteViaHostCompat$|FIB101" --output-on-failure`
   - 结果：**PASS（11/11）**
