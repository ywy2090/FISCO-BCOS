# `bcos/` — bcos-evm-bcos FISCO 编排外壳

本目录编译为静态库 **`bcos-evm-bcos`**，依赖 `bcos-evm-eth`。

## 四件套

| 组件 | 文件 | 职责 |
| --- | --- | --- |
| 执行桥 | `FiscoExecute.*` | 入口 `applyFiscoMessage()`：组装上下文 → `stateTransitionExecute` |
| 编排 | `FiscoOrchestrationProfile.*` | `OrchestrationProfile::bind` → pipeline hooks + error policy |
| VM 策略 | `FiscoVmHostPolicy.*` | 实现 `VmHostPolicy`（selfdestruct、precompile、nonce 等） |
| 费用结算 | `FiscoTxFeeSettlement.h` | `buyGas` / `refundGas` / `makeReceipt` / `consumeBalance` |

## 扩展点

| 目录/文件 | 职责 |
| --- | --- |
| `ports/AuthPort.h` | 权限表检查（依赖倒置，ADR-017） |
| `ports/ChainPrecompilePort.h` | 链精编译分发 |
| `FiscoPolicy.h` | Revision + feature flag 掩码 |
| `FiscoStateView.*` | FISCO 状态视图适配 |

## 执行流

```text
TransactionExecutorImpl → applyFiscoMessage() → stateTransitionExecute() → innerExecute()
```

测试见 `test/bcos/`。
