# bcos-evm-ref/opstack 向量 JSON schema（M4 首交付物）

复用 M-T 的 op-test-vectors 兼容 profile（`2026-07-09-mt-t8n-gate-opstack.md`）。本文档只定义
字段 → `bcos-evm-ref/opstack` 类型的映射；期望值只能由 opt8n 真跑产出（预注册纪律）。

## env → OpFeeParams（unpackOpFeeParams 的输入来源）
| JSON 字段 | L1Block 槽 | 本模块字段 |
|-----------|-----------|-----------|
| `_op_l1_base_fee`         | slot 1        | `OpFeeParams.l1_base_fee` |
| `_op_base_fee_scalar`     | slot 3[16,20) | `OpFeeParams.base_fee_scalar` |
| `_op_blob_base_fee_scalar`| slot 3[20,24) | `OpFeeParams.blob_base_fee_scalar` |
| `_op_blob_base_fee`       | slot 7        | `OpFeeParams.blob_base_fee` |
| `_op_operator_fee_scalar` | slot 8[20,24) | `OpFeeParams.operator_fee_scalar` |
| `_op_operator_fee_constant`| slot 8[24,32)| `OpFeeParams.operator_fee_constant` |

## transactions[]._op_deposit → DepositTx
| JSON 字段 | 本模块字段 |
|-----------|-----------|
| `source_hash` | `DepositTx.source_hash` |
| `from`        | `DepositTx.from` |
| `to`（null=创建） | `DepositTx.to`（nullopt） |
| `mint`（缺省/null=无 mint） | `DepositTx.mint`（nullopt=不加余额） |
| `value`       | `DepositTx.value` |
| `gasLimit`    | `DepositTx.gas_limit` |
| `is_system_tx`| `DepositTx.is_system_tx` |
| `data`        | `DepositTx.data` |

## fork
`_info.hardforks: ["isthmus"]` → `isthmusConfig()`；Jovian 留扩展位。

## 期望段（M5 harness 消费；E-b 前不做 t8n 全量 gate）
`postState`（被触碰账户 balance/nonce/code/非零槽）+ `_op_expected.receipts`
（status/gasUsed/logsCount/_op_deposit_nonce/_op_deposit_receipt_version）。
四 Vault 余额为差分主判据之一。
