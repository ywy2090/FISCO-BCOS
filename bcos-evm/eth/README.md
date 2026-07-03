# `eth/` — bcos-evm-eth 共享内核

本目录编译为静态库 **`bcos-evm-eth`**：标准 EVM 执行内核 + 以太坊参考路径。

## 依赖规则（硬约束）

- **禁止** `#include` `bcos/` 或 `opstack/` 头文件（ADR-005 Rule 1）。
- 链差异通过 `StateTransitionHooks`（state-transition 前后）和 `EvmHostHooks`（evmone 调用树内）注入。

## 子目录

| 目录 | 职责 |
| --- | --- |
| `apply/` | ETH 参考链编排（ApplyMessage、`EthEvmHostHooks`、`EthStateTransitionHooks`、precheck） |
| `settlement/` | State-based 费用结算（Projection、FeeSettlement、NormalTxFeeCoordinator） |
| `kernel/` | 可移植执行内核（Tier 2–3；三链共用，含 `EVMCResult` 边界类型） |
| `kernel/state-transition/` | `stateTransitionExecute` 共享内核步骤（ADR-019；geth `stateTransition.execute`） |
| `kernel/execution/` | 交易入口预热、`innerExecute`、`EvmCallFrame`、EIP-2929 warm pin |
| `core/` | 内核共享接口（`StateTransitionHooks`、`EvmHostHooks`、`ChainCallTargetPort`、`CallTargetTypes` 等 ADR-019/024 seam） |
| `eip/` | 单 EIP 实现（1559/2930/4844/7623/7702 等）与 revision 门控 |
| `gas/` | 跨 EIP 协议 gas（intrinsic、fee settlement、通用常量） |
| `policy/` | `EthChainPolicy`（revision / feature 策略） |
| `precompiled/` | `PrecompileRouter`、`PrecompileActive`、`EthPrecompiles`（legacy registry 在 `bcos-executor/src/vm/`） |
| `host/` | `EthHost.h` evmone host 实现（ADR-020 Legacy Enclave） |
| `state/` | State/Transition 数据层 |
| `vm/` | evmone 实例封装 |
| `trace/` | EVM 追踪 |

## `kernel/` — 可移植内核文件

| 文件 | 角色 |
| --- | --- |
| `kernel/execution/InnerExecute.*` | 内核执行入口 `innerExecute()`（geth innerExecute） |
| `kernel/state-transition/StateTransitionExecute.*` | `stateTransitionExecute()`（geth stateTransition.execute） |
| `kernel/state-transition/StateTransitionContext.h` | 交易级上下文 |
| `kernel/state-transition/DeductIntrinsicGas.h` | `deductIntrinsicGas()` |
| `kernel/state-transition/IncludedTxVmerrNormalize.h` | included-tx vmerr 归一化 |
| `kernel/execution/EvmCallFrame.*` | `runCallFrame()` / evm.Call 族 |
| `kernel/execution/FrameRouting.*` | 帧 message 路由 + `executionAddress`（7702 / CREATE warm pin） |
| `kernel/execution/FrameBytecode.*` | 帧 bytecode 加载（CREATE initcode / 7702 delegate） |
| `kernel/execution/CreateAddress.*` | CREATE / CREATE2 地址预测 |
| `kernel/execution/CreateDeployment.h` | CREATE 生命周期（assign / touch / code deposit） |
| `kernel/execution/FrameScope.h` | `TopLevel` / `Nested` 帧作用域 |
| `kernel/EVMCResult.*` | EVMC ↔ `TransactionStatus` 桥接（`adoptEvmcResult`） |
| `RevisionConfig.h` | EIP 开关位域（`eth/` 根） |

## `core/` — 链无关 seam 类型与端口

| 文件 | 角色 |
| --- | --- |
| `CallTargetTypes.h` | `CallTargetKind`、`WarmPolicy`、`CallTargetDescriptor`（ADR-024；链 adapter 与 kernel 共用） |
| `ChainCallTargetPort.h` | 链扩展 call target 注入端口 |
| `StateTransitionHooks.*` | 状态转换 hook 接口 + 默认 `innerExecute` 网关 |
| `EvmHostHooks.*` | evmone 调用树内 host 策略接口 |

## `eip/` — EIP 实现

| 文件 | 角色 |
| --- | --- |
| `Eip2930AccessList.h` | EIP-2930 access list 类型 |
| `Eip1559Gate.h` | EIP-1559 revision 门控（唯一读 `cfg.eip1559` 处） |
| `Eip2929Gate.h` | EIP-2929 / 3651 revision 门控（Scheme A；唯一读 `cfg.eip2929` 处） |
| `Eip2929StorageGas.h` | EIP-2929 / 2200 / 3529 SLOAD/SSTORE gas 常量 |
| `Eip7702.*` | EIP-7702 单点实现 |
| `Eip1559.h` / `Eip4844.h` / `Eip7623.h` 等 | 单 EIP gas 数学 |

## `gas/` — 跨 EIP 协议 gas

| 文件 | 角色 |
| --- | --- |
| `ProtocolGas.h` | 21000、CREATE、calldata、access list 等通用常量 |
| `GasSettlementTypes.h` | 结算 DTO（`TxIntrinsicGas`、`FeeInputs`、`PostExecuteGasResult` 等） |
| `TxGasLifecycle.h` | 正常交易 gas 生命周期索引（只读导航） |
| `TxIntrinsicGas.h` | intrinsic gas 公式（7623/2930/7702 预执行扣减） |
| `TopLevelGasSettlement.h` | 顶层 post-EVM gasUsed 结算（3529 refund cap、7623 floor） |
| `PostExecuteGasMetering.h` | 执行后 gasUsed 计量（State-free） |
| `TxGasUsedGate.h` | TE/EEST `finalizeEthTxGasUsed` fork 门控 |
| `TxFeeSettlement.h` | EIP-1559 费用投影（sync、State-free） |

## `precompiled/` — 预编译 gas 命名

| 文件 | 角色 |
| --- | --- |
| `Eip2537Gas.h` | EIP-2537 BLS12-381 MSM 折扣表 |
| `ModexpGas.h` | modexp (0x05) 跨 EIP-198/2565/7883 gas + EIP-7823 校验 |

## `apply/` — ETH 参考路径

| 文件 | 角色 |
| --- | --- |
| `ApplyEthMessage.*` | 链入口 `applyEthMessage()`（geth `ApplyMessage`；ADR-030 Tier C） |
| `EthEvmHostHooks.h` | ETH 默认 `EvmHostHooks` 实现（注入 `EthHost`） |
| `EthStateTransitionBindings.*` | `bind()` → 填充 `StateTransitionHooks` + `StateTransitionErrorPolicy` |
| `EthStateTransitionHooks.*` | 参考路径 `onPreCheckRules` 等 hook 实现（含交易预检规则） |

## `settlement/` — State-based 费用结算

Eth 参考路径费用生命周期（ADR-026 PR3；无 `bcos-ledger` / `EVMAccount` 依赖）。`TxFeeSettlement.h` 提供 sync plan；本目录 adapter 通过 `ctx.state` 落账。

| 文件 | 角色 |
| --- | --- |
| `EthSettlementProjection.h` | `ctx` + `EthMessageRequest` 只读投影 |
| `EthFeeSidecar.h` | buyGas 快照（`effectiveGasPrice` 等） |
| `EthFeeSettlement.*` | sync `ctx.state`：`buyGas` / `refundGas`；burn base；buyGas 余额不足 penalty（不 revert） |
| `EthNormalTxFeeCoordinator.*` | normal 路径编排（`buyGas` → `stateTransitionExecute` → `completeAfterPipeline`；含 pre-exec abort） |

## 链入口命名（ADR-029 + ADR-030）

三条链的 L1 入口均映射 geth `ApplyMessage`（ADR-030 §2）。Tier E `*Execute` 符号已于 ADR-032 Wave 4（2026-06-30）移除；`transaction-executor` 调用 `apply*Message`。

| geth | ADR-030 文档名（Tier C） | 头文件 | TE 调用 |
| --- | --- | --- | --- |
| `ApplyMessage` | `applyEthMessage` | `eth/apply/ApplyEthMessage.h` | `applyEthMessage` |
| `ApplyMessage` | `applyFiscoMessage` | `bcos/ApplyFiscoMessage.h` | `applyFiscoMessage` |
| `ApplyMessage` + op lifecycle | `applyOpStackMessage` | `opstack/ApplyOpStackMessage.h` | `applyOpStackMessage` |

**ApplyMessage DTO（P1-2，方案 A）：** 三链统一 `*MessageRequest` / `*MessageResult` — `EthMessageRequest`、`FiscoMessageRequest`、`OpStackMessageRequest`（及对应 `*Result`）。geth `ExecutionResult` 语义由 Result 侧字段承载（ADR-030 §5）。

内核 tx 级执行见 `innerExecute`（geth post-`Prepare` 路径；ADR-030 §3 step 6）。geth 词汇对照见 ADR-030 §3–§8（canonical 符号：`stateTransitionExecute`、`innerExecute`、`runCallFrame`、`prepareState` 等）。

## 执行流

```text
eth/apply/applyEthMessage()  // geth: ApplyMessage — ADR-030 文档名
  └─ EthStateTransitionBindings::bind
       └─ eth/kernel/state-transition/stateTransitionExecute()
            └─ onInvokeInnerExecute → eth/kernel/execution/innerExecute()
```

详见 `docs/architecture-overview.md`、`docs/adr/019-orchestration-pipeline.md`、`docs/adr/030-geth-naming-map.md`、**ADR-033**（磁盘文件名波次）。
