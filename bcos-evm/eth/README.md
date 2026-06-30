# `eth/` — bcos-evm-eth 共享内核

本目录编译为静态库 **`bcos-evm-eth`**：标准 EVM 执行内核 + 以太坊参考路径。

## 依赖规则（硬约束）

- **禁止** `#include` `bcos/` 或 `opstack/` 头文件（ADR-005 Rule 1）。
- 链差异通过 `TxPipelineHooks`（管线前后）和 `VmHostPolicy`（evmone 调用树内）注入。

## 子目录

| 目录 | 职责 |
| --- | --- |
| `core/` | 内核共享接口（`StateTransitionHooks`、`EvmHostHooks`、`ChainExtendedPrecompileDispatch` 等 ADR-019/024 seam） |
| `pipeline/` | `stateTransitionExecute` 共享管线步骤（ADR-019） |
| `apply/` | ETH 参考链编排（ApplyMessage 适配、hooks、precheck、fee settlement） |
| `execution/` | 交易入口预热、`innerExecute`、`EvmCallFrame`、EIP-2929 access gate |
| `eip/` | EIP 实现（1559/2930/4844/7623/7702 等 gas 与 access 语义） |
| `policy/` | `VmHostPolicy` / `EthVmHostPolicy` / `EthChainPolicy`（revision 策略） |
| `precompiled/` | `PrecompileRouter`、builtin registry |
| `host/` | `EthHost` evmone host 实现（ADR-020 Legacy Enclave） |
| `state/` | State/Transition 数据层 |
| `vm/` | evmone 实例封装 |
| `trace/` | EVM 追踪 |

## 根目录文件（内核）

| 文件 | 角色 |
| --- | --- |
| `execution/InnerExecute.*` | 内核执行入口 `innerExecute()`（geth innerExecute） |
| `pipeline/StateTransitionExecute.*` | `stateTransitionExecute()`（geth stateTransition.execute） |
| `pipeline/StateTransitionContext.h` | 管线上下文 |
| `pipeline/DeductIntrinsicGas.h` | `deductIntrinsicGas()` |
| `pipeline/IncludedTxVmerrNormalize.h` | included-tx vmerr 归一化 |
| `execution/EvmCallFrame.*` | `runCallFrame()` / evm.Call 族 |
| `RevisionConfig.h` | EIP 开关位域 |
| `EVMCResult.*` | EVMC 结果封装 |
| `EthExecutionArtifacts.h` | 参考路径执行上下文（TE 消费） |

## `eip/` — EIP 实现

| 文件 | 角色 |
| --- | --- |
| `Eip2930AccessList.h` | EIP-2930 access list 类型 |
| `Eip7702.*` | EIP-7702 单点实现 |
| `Eip1559.h` / `Eip4844.h` / `Eip7623.h` 等 | gas 数学与 intrinsic 结算 |

## `apply/` — ETH 参考路径

| 文件 | 角色 |
| --- | --- |
| `ApplyReferenceMessage.*` | 链入口 `applyReferenceMessage()`（geth `ApplyMessage`；ADR-030 Tier C） |
| `EthOrchestrationProfile.*` | `OrchestrationProfile::bind` → 填充 `StateTransitionHooks` + `StateTransitionErrorPolicy` |
| `EthTxPrecheck.*` | 参考路径交易预检 |
| `EthTxFeeSettlement.h` | `buyGas` / `refundGas` 等 |

## 链入口命名（ADR-029 + ADR-030）

三条链的 L1 入口均映射 geth `ApplyMessage`（ADR-030 §2）。Tier E `*Execute` 符号已于 ADR-032 Wave 4（2026-06-30）移除；`transaction-executor` 调用 `apply*Message`。

| geth | ADR-030 文档名（Tier C） | 头文件 | TE 调用 |
| --- | --- | --- | --- |
| `ApplyMessage` | `applyReferenceMessage` | `eth/apply/ApplyReferenceMessage.h` | `applyReferenceMessage` |
| `ApplyMessage` | `applyFiscoMessage` | `bcos/ApplyFiscoMessage.h` | `applyFiscoMessage` |
| `ApplyMessage` + op lifecycle | `applyOpStackMessage` | `opstack/ApplyOpStackMessage.h` | `applyOpStackMessage` |

内核 tx 级执行见 `innerExecute`（geth post-`Prepare` 路径；ADR-030 §3 step 6）。geth 词汇对照见 ADR-030 §3–§8（canonical 符号：`stateTransitionExecute`、`innerExecute`、`runCallFrame`、`warmTransactionEntry` 等）。

## 执行流

```text
applyReferenceMessage()  // geth: ApplyMessage — ADR-030 文档名
  └─ EthOrchestrationProfile::bind
       └─ stateTransitionExecute()   // geth: stateTransition.execute
            └─ onInvokeInnerExecute → innerExecute()   // geth: innerExecute
```

详见 `docs/architecture-overview.md`、`docs/adr/019-orchestration-pipeline.md`、`docs/adr/030-geth-naming-map.md`、**ADR-033**（磁盘文件名波次）。
