# `eth/` — bcos-evm-eth 共享内核

本目录编译为静态库 **`bcos-evm-eth`**：标准 EVM 执行内核 + 以太坊参考路径。

## 依赖规则（硬约束）

- **禁止** `#include` `bcos/` 或 `opstack/` 头文件（ADR-005 Rule 1）。
- 链差异通过 `TxPipelineHooks`（管线前后）和 `VmHostPolicy`（evmone 调用树内）注入。

## 子目录

| 目录 | 职责 |
| --- | --- |
| `pipeline/` | `runTxPipeline` 共享管线步骤（ADR-019） |
| `apply/` | ETH 参考链编排（ApplyMessage 适配、hooks、precheck、fee settlement） |
| `execution/` | 交易入口预热、BlockInfo、EIP-2929 access gate、ExecutionFrame |
| `gas/` | 1559/4844/7623 等纯 gas 数学 |
| `policy/` | `VmHostPolicy` / `EthVmHostPolicy` / `EthChainPolicy`（revision 策略） |
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
| `EthExecutionArtifacts.h` | 参考路径执行上下文（TE 消费） |

## `apply/` — ETH 参考路径

| 文件 | 角色 |
| --- | --- |
| `EthReferenceExecute.*` | 链入口：`applyReferenceMessage()`（ADR-030 文档名）/ `ethReferenceExecute()`（Tier E 稳定 ABI） |
| `EthOrchestrationProfile.*` | `OrchestrationProfile::bind` → 填充 `ChainPrecheckPolicy` + `OrchestrationErrorPolicy` |
| `EthTxPrecheck.*` | 参考路径交易预检 |
| `EthTxFeeSettlement.h` | `buyGas` / `refundGas` 等 |

## 链入口命名（ADR-029 + ADR-030 双标签）

三条链的 L1 入口均映射 geth `ApplyMessage`（ADR-030 §2）。**文档与注释优先使用 `apply*Message`**；`*Execute` 保留为 Tier E 稳定 ABI，供 TE / executor 链接，暂不标记 `[[deprecated]]`。

| geth | ADR-030 文档名（Tier C） | ADR-029 / 稳定 ABI（Tier E） | 头文件 |
| --- | --- | --- | --- |
| `ApplyMessage` | `applyReferenceMessage` | `ethReferenceExecute` | `eth/apply/EthReferenceExecute.h` |
| `ApplyMessage` | `applyFiscoMessage` | `fiscoExecute` | `bcos/FiscoExecute.h` |
| `ApplyMessage` + op lifecycle | `applyOpStackMessage` | `opStackExecute` | `opstack/OpStackExecute.h` |

**阅读规则：** `*Execute` = 该链的 ApplyMessage 适配器，不是泛指的“跑 EVM”。内核 tx 级执行见 `executeMessage`（geth `innerExecute` 别名，ADR-030 §3 step 6）。

Tier A geth 别名索引见 `GethNamingAliases.h`（`stateTransitionExecute`、`innerExecute`、`evmCall` 等）。

## 执行流

```text
applyReferenceMessage()  // geth: ApplyMessage — ADR-030 文档名
  └─ ethReferenceExecute()   // Tier E 稳定 ABI（等价转发）
       └─ EthOrchestrationProfile::bind
            └─ runTxPipeline()   // geth: stateTransition.execute — ADR-030 stateTransitionExecute
                 └─ pipelineInvokeEvmKernel → executeMessage()   // geth: innerExecute
```

详见 `docs/architecture-overview.md`、`docs/adr/019-orchestration-pipeline.md`、`docs/adr/030-geth-naming-map.md`。
