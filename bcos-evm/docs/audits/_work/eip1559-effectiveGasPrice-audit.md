# EIP-1559 PR — `effectiveGasPrice` 调用点审计

**日期：** 2026-06-21  
**命令：** `rg effectiveGasPrice transaction-executor/bcos-transaction-executor bcos-evm/eth`  
**Spec：** `docs/superpowers/specs/2026-06-21-eth-eip1559-settlement-design.md` §2.3

| 文件 | 行 | eth TE 路径 | 本 PR 动作 |
|------|-----|-------------|------------|
| `EthTransactionExecutorImpl.h` | 143 | ✅ Prepare warm | **改** — 删除 `protocol::effectiveGasPrice`；warm 用 legacy `gasPrice` 或省略 |
| `EthTransactionExecutorImpl.h` | 224 | ✅ executeViaEthTx | **改** — 传 `m_gasPriceLegacy` + caps；ExecuteViaEth 内 normalize |
| `EthTxExecutor.h` | 31 | ✅ buyGas | **改** — `gas::resolveEffectiveGasPrice` + `maxBalanceGasDebit` |
| `EthTxExecutor.h` | 87 | ✅ refundGas | **改** — 用 `m_effectiveGasPrice` + `tipPerGas` |
| `TransactionExecutorImpl.h` | 171, 276 | ❌ FISCO TE | **不变** |
| `OpStackTransactionExecutorImpl.h` | 114, 172, 244 | ❌ OpStack | **不变**（OpStack 去重 defer） |
| `OpStackTxInputBuilder.h` | 98 | ❌ OpStack | **不变** |

**结论：** eth TE 必改 4 处（Task 3–5）；其余路径留 §2.3 已知债务。
