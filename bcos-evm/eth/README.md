# `eth/` — bcos-evm-eth 共享内核

本目录编译为静态库 **`bcos-evm-eth`**：标准 EVM 执行内核 + 以太坊参考路径。

## 依赖规则（硬约束）

- **禁止** `#include` `bcos/` 或 `opstack/` 头文件（ADR-005 Rule 1）。
- 链差异通过 `TxPipelineHooks`（管线前后）和 `VmHostPolicy`（evmone 调用树内）注入。

## 子目录

| 目录 | 职责 |
| --- | --- |
| `pipeline/` | `runTxPipeline` 共享管线步骤（ADR-019） |
| `reference/` | ETH 参考链编排（bridge、hooks、precheck、fee ledger） |
| `execution/` | 交易入口预热、BlockInfo、EIP-2929 access gate、ExecutionFrame |
| `gas/` | 1559/4844/7623 等纯 gas 数学 |
| `policy/` | `VmHostPolicy` / `EthVmHostPolicy` / `EthPolicy`（revision 策略） |
| `precompiled/` | `PrecompileRouter`、builtin registry |
| `state/` | State/Host/Transition（Legacy Enclave，见 ADR-020） |
| `vm/` | evmone 实例封装 |
| `trace/` | EVM 追踪 |

## 根目录文件（内核）

| 文件 | 角色 |
| --- | --- |
| `ExecuteMessage.*` | 内核执行入口 `executeMessage()` |
| `RevisionConfig.h` | EIP 开关位域 |
| `Eip7702.*` | EIP-7702 单点实现 |
| `EVMCResult.*` | EVMC 结果封装 |
| `EthExecutionContext.h` | 参考路径执行上下文（TE 消费） |

## `reference/` — ETH 参考路径

| 文件 | 角色 |
| --- | --- |
| `EthReferenceBridge.*` | 入口 `ethReferenceExecute()` |
| `EthPipelineHookBinder.*` | 填充 `TxPipelineHooks` |
| `EthTxPrecheck.*` | 参考路径交易预检 |
| `EthTxFeeLedger.h` | `buyGas` / `refundGas` 等 |

## 执行流

```text
ethReferenceExecute() → runTxPipeline(ctx, hooks) → executeMessage()
```

详见 `docs/architecture-overview.md` 与 `docs/adr/019-orchestration-pipeline.md`。
