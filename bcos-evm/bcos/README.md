# `bcos/` — bcos-evm-bcos FISCO 编排外壳

本目录编译为静态库 **`bcos-evm-bcos`**，依赖 `bcos-evm-eth`。

## 四件套

| 组件 | 文件 | 职责 |
| --- | --- | --- |
| 执行桥 | `FiscoExecutionBridge.*` | 入口 `fiscoExecute()`：组装上下文 → `runTxPipeline` |
| 钩子绑定 | `FiscoPipelineHookBinder.*` | 填充 `TxPipelineHooks` |
| VM 策略 | `FiscoVmHostPolicy.*` | 实现 `VmHostPolicy`（selfdestruct、precompile、nonce 等） |
| 费用账本 | `FiscoTxFeeLedger.h` | `buyGas` / `refundGas` / `makeReceipt` / `consumeBalance` |

## 扩展点

| 目录/文件 | 职责 |
| --- | --- |
| `ports/AuthPort.h` | 权限表检查（依赖倒置，ADR-017） |
| `ports/ChainPrecompilePort.h` | 链精编译分发 |
| `FiscoPolicy.h` | Revision + feature flag 掩码 |
| `FiscoEvmStateReader.*` | FISCO 状态视图适配 |

## 执行流

```text
TransactionExecutorImpl → fiscoExecute() → runTxPipeline() → executeMessage()
```

测试见 `test/bcos/`。
