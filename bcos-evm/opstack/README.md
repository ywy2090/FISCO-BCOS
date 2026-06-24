# `opstack/` — bcos-evm-op OP Stack 编排外壳

本目录编译为静态库 **`bcos-evm-op`**，依赖 `bcos-evm-eth`。

## 四件套

| 组件 | 文件 | 职责 |
| --- | --- | --- |
| 执行桥 | `OpStackExecutionBridge.*` | 入口 `opStackExecute()` |
| 钩子绑定 | `OpStackPipelineHookBinder.*` | 填充 `TxPipelineHooks` |
| VM 策略 | `OpStackVmHostPolicy.h` | L1Block 预部署等 |
| 费用账本 | `OpStackTxFeeLedger.*` | `buyGas` / `refundGas` 等 |

## 领域模块

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| 交易预检 | `OpStackTxPrecheck.*` | nonce、1559、blob、7702 |
| Floor gas | `OpStackFloorGasPrecheck.*` | ③½ `preDebitEntry` 余额/floor 检查 |
| 费用 | `OpStackFee.*`、`RollupCost.*`、`OpStackFloorGas.*` | L1 fee、rollup、settlement |
| L1 | `L1Block*`、`GasPriceOracle*` | L1 属性与预部署 |

## 执行流

```text
OpStackTransactionExecutorImpl
  → buyGas (wrapper 外圈)
  → opStackExecute() → runTxPipeline()
  → refundGas + build_diff (wrapper 外圈)
```

测试见 `test/opstack/`。
