# Task 12 Report: ExecuteViaHost refactor + transition thin wrapper

## 完成内容

1. ExecuteViaHost 下沉核心执行到 `executeMessage`
   - 修改 `bcos-evm/bcos/ExecuteViaHost.cpp`：
     - 保留 FISCO 层职责：`deriveMessage`、auth check、EIP-7623 预扣与快照、`FiscoHostExtension` 注入。
     - 删除内联核心执行分支（手工 code-empty/NotFoundCodeError 分流、内联状态执行细节），统一委托 `executeMessage(...)`。
     - 透传 `gasPrice` 到 `executeMessage`，保持 tx context 的 gas price 信息。
   - `bcos-evm/bcos/ExecuteViaHost.h` 接口保持不变（仍是 orchestration 层入口）。

2. executeMessage 补齐 transition 复用所需上下文
   - 修改 `bcos-evm/eth/executeMessage.h`：
     - `ExecuteMessageInput` 新增 `gasPrice` 字段。
   - 修改 `bcos-evm/eth/executeMessage.cpp`：
     - `tx_context.tx_gas_price` 由 `input.gasPrice` 填充。
     - 增加顶层内置预编译分发（仅在目标地址无部署代码时触发），使 transition 薄包装后仍保持预编译执行语义。

3. transition 改为薄包装
   - 修改 `bcos-evm/eth/state/transition.cpp`：
     - 移除 `transition` 内部的 warm/host/vm.execute/commit-revert 主流程。
     - 仅构造 top-level `evmc_message`，调用 `executeMessage(...)`，并做 `TransactionReceipt` 转换（status/output/gasUsed/gasRefund）。
     - 传入 `vm`、`block_hashes`、`gasPrice`、`tx_props`、`ext`，满足“thin wrapper around executeMessage + receipt conversion”。

## 验证结果

1. `bcos-evm` 测试集
   - 命令：`rtk ctest --test-dir build/bcos-evm/test --output-on-failure`
   - 结果：`100% tests passed, 0 tests failed out of 14`

2. `transaction-executor` 兼容测试（可构建）
   - 命令：`rtk ctest --test-dir build/transaction-executor/tests -R ExecuteViaHostCompat --output-on-failure`
   - 结果：`100% tests passed, 0 tests failed out of 7`

