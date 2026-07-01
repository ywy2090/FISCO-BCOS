# `eth/` — bcos-evm-eth 共享内核

本目录编译为静态库 **`bcos-evm-eth`**：标准 EVM 执行内核 + 以太坊参考路径。

## 依赖规则（硬约束）

- **禁止** `#include` `bcos/` 或 `opstack/` 头文件（ADR-005 Rule 1）。
- 链差异通过 `StateTransitionHooks`（state-transition 前后）和 `VmHostPolicy`（evmone 调用树内）注入。

## 子目录

| 目录 | 职责 |
| --- | --- |
| `apply/` | ETH 参考链编排（ApplyMessage 适配、hooks、precheck、fee settlement） |
| `kernel/` | 可移植执行内核（Tier 2–3；三链共用，含 `EVMCResult` 边界类型） |
| `kernel/state-transition/` | `stateTransitionExecute` 共享内核步骤（ADR-019；geth `stateTransition.execute`） |
| `kernel/execution/` | 交易入口预热、`innerExecute`、`EvmCallFrame`、EIP-2929 warm pin |
| `core/` | 内核共享接口（`StateTransitionHooks`、`EvmHostHooks`、`ChainExtendedPrecompileDispatch` 等 ADR-019/024 seam） |
| `eip/` | 单 EIP 实现（1559/2930/4844/7623/7702 等）与 revision 门控 |
| `gas/` | 跨 EIP 协议 gas（intrinsic、fee settlement、通用常量） |
| `policy/` | `VmHostPolicy` / `EthVmHostPolicy` / `EthChainPolicy`（revision 策略） |
| `precompiled/` | `PrecompileRouter`、builtin registry |
| `host/` | `EthHost` evmone host 实现（ADR-020 Legacy Enclave） |
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
| `kernel/EVMCResult.*` | EVMC ↔ `TransactionStatus` 桥接（`adoptEvmcResult`） |
| `RevisionConfig.h` | EIP 开关位域（`eth/` 根） |

## `eip/` — EIP 实现

| 文件 | 角色 |
| --- | --- |
| `Eip2930AccessList.h` | EIP-2930 access list 类型 |
| `Eip2929Gate.h` | EIP-2929 / 3651 revision 门控（Scheme A；唯一读 `cfg.eip2929` 处） |
| `Eip2929StorageGas.h` | EIP-2929 / 2200 / 3529 SLOAD/SSTORE gas 常量 |
| `Eip7702.*` | EIP-7702 单点实现 |
| `Eip1559.h` / `Eip4844.h` / `Eip7623.h` 等 | 单 EIP gas 数学 |

## `gas/` — 跨 EIP 协议 gas

| 文件 | 角色 |
| --- | --- |
| `ProtocolGas.h` | 21000、CREATE、calldata、access list 等通用常量 |
| `TxIntrinsicGas.h` | intrinsic gas 与 top-level settlement（7623/3529/2930/7702 组合） |
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
| `EthStateTransitionBindings.*` | `bind()` → 填充 `StateTransitionHooks` + `StateTransitionErrorPolicy` |
| `EthTxPrecheck.*` | 参考路径交易预检 |
| `EthTxFeeSettlement.h` | `buyGas` / `refundGas` 等 |

## 链入口命名（ADR-029 + ADR-030）

三条链的 L1 入口均映射 geth `ApplyMessage`（ADR-030 §2）。Tier E `*Execute` 符号已于 ADR-032 Wave 4（2026-06-30）移除；`transaction-executor` 调用 `apply*Message`。

| geth | ADR-030 文档名（Tier C） | 头文件 | TE 调用 |
| --- | --- | --- | --- |
| `ApplyMessage` | `applyEthMessage` | `eth/apply/ApplyEthMessage.h` | `applyEthMessage` |
| `ApplyMessage` | `applyFiscoMessage` | `bcos/ApplyFiscoMessage.h` | `applyFiscoMessage` |
| `ApplyMessage` + op lifecycle | `applyOpStackMessage` | `opstack/ApplyOpStackMessage.h` | `applyOpStackMessage` |

内核 tx 级执行见 `innerExecute`（geth post-`Prepare` 路径；ADR-030 §3 step 6）。geth 词汇对照见 ADR-030 §3–§8（canonical 符号：`stateTransitionExecute`、`innerExecute`、`runCallFrame`、`warmTransactionEntry` 等）。

## 执行流

```text
eth/apply/applyEthMessage()  // geth: ApplyMessage — ADR-030 文档名
  └─ EthStateTransitionBindings::bind
       └─ eth/kernel/state-transition/stateTransitionExecute()
            └─ onInvokeInnerExecute → eth/kernel/execution/innerExecute()
```

详见 `docs/architecture-overview.md`、`docs/adr/019-orchestration-pipeline.md`、`docs/adr/030-geth-naming-map.md`、**ADR-033**（磁盘文件名波次）。
