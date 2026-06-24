# `opstack/` — bcos-evm-op OP Stack 编排外壳

本目录编译为静态库 **`bcos-evm-op`**，依赖 `bcos-evm-eth`。

## 目录

| 路径 | 职责 |
| --- | --- |
| 根目录 | 四件套（Bridge / HookBinder / VmHostPolicy / TxFeeLedger）、TxPrecheck、常量 |
| `fee/` | L1 fee、rollup cost、floor gas、gas settlement |
| `l1/` | L1Block 与 GasPriceOracle 预部署 |

## 四件套（根目录）

| 组件 | 文件 | 职责 |
| --- | --- | --- |
| 执行桥 | `OpStackExecutionBridge.*` | 入口 `opStackExecute()` |
| 钩子绑定 | `OpStackPipelineHookBinder.*` | 填充 `TxPipelineHooks` |
| VM 策略 | `OpStackVmHostPolicy.h` | L1Block 预部署等 |
| 费用账本 | `OpStackTxFeeLedger.*` | `buyGas` / `refundGas` 等 |

## 子目录模块

| 目录 | 文件 | 职责 |
| --- | --- | --- |
| 根 | `OpStackTxPrecheck.*` | nonce、1559、blob、7702 |
| `fee/` | `OpStackFloorGasPrecheck.*` | ③½ `preDebitEntry` 余额/floor 检查 |
| `fee/` | `OpStackFee.*`、`RollupCost.*`、`OpStackFloorGas.*`、`OpStackGasSettlement.h` | L1 fee、rollup、settlement |
| `l1/` | `L1Block*`、`GasPriceOracle*` | L1 属性与预部署 |

## 执行流

```text
OpStackTransactionExecutorImpl
  → buyGas (wrapper 外圈)
  → opStackExecute() → runTxPipeline()
  → refundGas + build_diff (wrapper 外圈)
```

测试见 `test/opstack/`。
