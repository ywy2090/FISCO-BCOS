# `opstack/` — bcos-evm-op OP Stack 编排外壳

本目录编译为静态库 **`bcos-evm-op`**，依赖 `bcos-evm-eth`。

## 目录

| 路径 | 职责 |
| --- | --- |
| 根目录 | Bridge、Lifecycle、Settlement、OrchestrationProfile、Precheck、常量 |
| `fee/` | L1 fee、rollup cost、floor gas、gas settlement |
| `l1/` | L1Block 与 GasPriceOracle 预部署 |

## 核心模块（根目录）

| 组件 | 文件 | 职责 |
| --- | --- | --- |
| 执行桥 | `OpStackExecutionBridge.*` | TE 稳定入口 `opStackExecute()` → 委托 lifecycle |
| 生命周期 | `OpStackTxLifecycle.*` | Deep module `runOpStackTxLifecycle`（gasPool、分支、settlement 编排） |
| 费用投影 | `OpStackSettlementView.*`, `OpStackFeeSidecar.h` | `ctx` + `input` + sidecar 只读投影；无 request 镜像 |
| Normal 结算 | `OpStackNormalFeeSettlement.*` | `buyGas` + `completeAfterPipeline`（ADR-025 内聚） |
| 同步结算 | `OpStackSettlement.*` | `finalizeNormal` / `finalizeDeposit` / `settleDeposit` / abort helpers |
| 费用账本 | `OpStackTxFeeLedger.*` | Adapter：`buyGas` / `refundGas`（读 `OpStackSettlementView`） |
| 编排 | `OpStackOrchestrationProfile.*` | `BindingsContext` `{ input, view }`；`bind` → pipeline hooks |
| 预检 | `OpStackPrecheckPolicy.*` | `checkEntryRules` + `checkGasAffordable` |
| 链 call target | `OpStackChainCallTargetAdapter.*` | L1Block / GasPriceOracle classify + dispatch |

## 子目录模块

| 目录 | 文件 | 职责 |
| --- | --- | --- |
| `fee/` | `OpStackFee.*`、`RollupCost.*`、`OpStackFloorGas.*`、`OpStackGasSettlement.h` | L1 fee、rollup、settlement |
| `l1/` | `L1Block*`、`GasPriceOracle*` | L1 属性与预部署 |

## 执行流（ADR-021 Appendix A）

```text
OpStackTransactionExecutorImpl
  → opStackExecute()
  → runOpStackTxLifecycle
      ├─ OpStackSettlementView { ctx, input, sidecar }
      ├─ OpStackOrchestrationProfile::bind(bindingsCtx)
      ├─ checkEntryRules
      ├─ deposit: gasPool → mint → pipeline → settleDeposit
      └─ normal:  gasPool → checkpoint → NormalFeeSettlement.buyGas
                  → pipeline → completeAfterPipeline
```

测试见 `test/opstack/`。设计见 ADR-021（Appendix A）、ADR-023、ADR-025。
