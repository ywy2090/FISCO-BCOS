# `opstack/` — bcos-evm-op OP Stack 编排外壳

本目录编译为静态库 **`bcos-evm-op`**，依赖 `bcos-evm-eth`。

## 子目录

| 路径 | 职责 |
| --- | --- |
| `apply/` | 链入口、生命周期、StateTransition bindings/hooks |
| `settlement/` | Projection、Sidecar、TxFinalize、FeeSettlement、NormalTxFeeCoordinator |
| `adapter/` | `OpStackChainCallTargetAdapter`（L1Block / GasPriceOracle 路由） |
| `policy/` | 常量、fork 时间表、Isthmus revision 绑定 |
| `types/` | DTO / 元数据（DepositTx、ReceiptMeta、BlobTxChecks、HeaderExtension） |
| `fee/` | L1 fee、rollup cost、floor gas、pre/post debit plan（纯计算） |
| `l1/` | L1Block 与 GasPriceOracle 预部署 |

## Settlement 命名对照

`Settlement` 一词在 opstack 内曾指代不同层级；P3 重命名后推荐符号如下（`OpStackFeeSettlement` 保留以对齐 `eth/EthTxFeeSettlement`）：

| 符号 / 文件 | 目录 | 层级 | 职责 | 是否改 state |
| --- | --- | --- | --- | --- |
| `OpStackSettlementProjection` | `settlement/` | 只读投影 | `ctx` + `input` + `sidecar` 统一读视图（ADR-030: `SettlementProjection`） | 否 |
| `OpStackFeeSidecar` | `settlement/` | 可变 sidecar | 生命周期内费用快照（effectiveGasPrice、l1CostCharged 等） | 是（内存） |
| `OpStackFeeSettlement` | `settlement/` | 异步账本 | `buyGas` / `refundGas`；对齐 `eth/EthTxFeeSettlement` | 是 |
| `OpStackTxFinalize` | `settlement/` | 同步收尾 | `finalizeNormal` / `finalizeDeposit` / `settleDeposit` / abort | 部分（gas pool） |
| `OpStackPostExecuteGas` | `fee/` | 纯 gas 数学 | `postExecuteGasSettlement`（refund、floor 扣减） | 否 |
| `OpStackNormalTxFeeCoordinator` | `settlement/` | 编排 | normal 路径：`buyGas` → pipeline → `completeAfterPipeline` | 委托上述组件 |

**调用顺序（normal）：** `Projection` → `FeeSettlement.buyGas`（读 `PreDebitPlan`）→ pipeline → `TxFinalize.finalizeNormal`（读 `PostExecuteGas`）→ `FeeSettlement.refundGas`（读 `PostSettlementPlan`）→ `NormalTxFeeCoordinator` 写 receipt。

**与 eth 对照：** `EthTxFeeSettlement` 合一；opstack 拆为 Projection + FeeSettlement + TxFinalize + fee 层 plan/math。

## `apply/` — 链入口 & 生命周期

| 文件 | 职责 |
| --- | --- |
| `ApplyOpStackMessage.*` | TE 入口 `applyOpStackMessage()`（gasPool、deposit/normal 分支、settlement 编排） |
| `OpStackStateTransitionBindings.*` | `Context` `{ input, view }`；`bind` → pipeline hooks |
| `OpStackStateTransitionHooks.*` | 入口规则、`onPreCheckGasAffordable`、inner execute 调优 |
| `OpStackStateTransitionErrorPolicy.h` | OpStack 错误处理策略 |
| `OpStackEvmResult.h` | apply 层 EVMCResult 辅助（对齐 `eth/EthEvmResult.h`） |

## `settlement/` — 费用结算编排

| 文件 | 职责 |
| --- | --- |
| `OpStackSettlementProjection.*` | `ctx` + `input` + sidecar 只读投影 |
| `OpStackFeeSidecar.h` | 生命周期内可变费用状态 |
| `OpStackNormalTxFeeCoordinator.*` | `buyGas` + `completeAfterPipeline`（ADR-025） |
| `OpStackTxFinalize.*` | `finalizeNormal` / `finalizeDeposit` / `settleDeposit` / abort |
| `OpStackFeeSettlement.*` | Adapter：`buyGas` / `refundGas` |

## `fee/` — 纯计算 & plan

| 文件 | 职责 |
| --- | --- |
| `OpStackPreDebitPlan.*` | 定义 `OpStackPreDebitInputs` / `OpStackPreDebitPlan`；`planOpStackPreDebit` |
| `OpStackPreDebitInputsMapping.h` | `toOpStackPreDebitInputs(projection)`（仅映射，对齐 `eth/EthFeeInputsMapping.h`） |
| `OpStackPostSettlementPlan.*` | 定义 `OpStackPostSettlementInputs` / `OpStackPostSettlementPlan` |
| `OpStackPostSettlementInputsMapping.h` | `toOpStackPostSettlementInputs(projection, settled)`（仅映射） |
| `OpStackPostExecuteGas.h` | 执行后 gas/refund/floor 数学（`GasSettlement`） |
| `OpStackFeeParams.*`、`RollupCost.*`、`OpStackFloorGas.*` | L1/operator cost、rollup、floor gas |

## 执行流（ADR-021 Appendix A）

```text
OpStackTransactionExecutorImpl
  → applyOpStackMessage()                    [apply/]
      ├─ OpStackSettlementProjection         [settlement/]
      ├─ OpStackStateTransitionBindings::bind [apply/]
      ├─ lifecycleCheckEntryRules            [apply/]
      ├─ deposit: gasPool → mint → pipeline → settleDeposit
      └─ normal:  gasPool → buyGas → pipeline → completeAfterPipeline
```

测试见 `test/opstack/`。设计见 ADR-021（Appendix A）、ADR-023、ADR-025。
