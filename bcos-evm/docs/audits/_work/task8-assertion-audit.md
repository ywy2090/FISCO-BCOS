# Task 8 — 测试断言审计（横向）

**日期：** 2026-06-20  
**Skill：** `fisco-evm-test-coverage/references/assertion-audit.md`  
**清单：** `_work/test-inventory.md`  
**金标准：** geth v1.17.3、`core/vm/testdata/precompiles/*.json`；Besu 26.6.0 referencetests（抽样）

---

## 断言等级说明

| 符号 | 含义 |
|------|------|
| ✅ | 期望值与金标准 / EthChainPolicy / EIP MUST 一致；路径有效 |
| 🟡 | 方向对但偏弱：helper-only、缺 post-state/gas、名实不符 smoke |
| 🔴 | 期望值错误、测错 API、revision 组合使断言无约束力 |

---

## 显式单元用例审计（16）

| 测试文件 | 用例 | 判定 | 金标准来源 | 备注 |
|----------|------|------|------------|------|
| `RevisionConfigProfileTest.cpp` | `revision_config_bool_field_macro_count` | ✅ | `RevisionConfig.h` 宏 | 结构完整性 |
| `RevisionConfigProfileTest.cpp` | `eth_policy_full_fork_snapshots` | ✅ | `EthChainPolicy.h:27-41` | 5 分叉 × 全字段；PRAGUE 无 `eip7702` 与源码一致 |
| `RevisionConfigProfileTest.cpp` | `fisco_policy_feature_gate_snapshots` | 🟡 | `FiscoPolicy.h` | 测 FiscoPolicy 非 EthChainPolicy baseline；PRAGUE 行 `eip7702=true` 记录路径分裂 |
| `RevisionConfigProfileTest.cpp` | `isthmus_helper_sparse_profile_all_fields` | 🟡 | `makeIsthmusRevisionConfig()` | OPStack helper；`eip7702=true` 与 EthChainPolicy 不同 |
| `EthTxInputBuilderTest.cpp` | `fillWeb3Fields_maps_eip7702_authorizations` | ✅ | EIP-7702 type-4 RLP + ecrecover | 仅 input 层；不测 apply/post-state |
| `FiscoTxInputBuilderTest.cpp` | `fillWeb3Fields_maps_eip7702_authorizations` | 🟡 | 同上 | 重复覆盖 FISCO builder；非 ETH reference |
| `Eip2537KernelTest.cpp` | `stBLS_add_precompile_0x0b_via_executeMessage` | ✅ | geth `blsG1Add.json` case `bls_g1add_(inf+g1=g1)` | `executeMessage` + `eip2537=true`；output 128B 一致 |
| `TxFeaturePrepareTest.cpp` | `setWarmDestinationFromKind_matches_create_vs_call` | ✅ | geth `Prepare` skip dst on CREATE | helper；与 `warmTransactionEntry` 组合有效 |
| `Eip2929AccessHostTest.cpp` | `access_account_cold_then_warm` | ✅ | EIP-2929 §Cold/warm | 生产 `EthHost::access_account` |
| `Eip2929AccessHostTest.cpp` | `access_storage_cold_then_warm` | ✅ | EIP-2929 | 生产 `EthHost::access_storage` |
| `Eip2929AccessHostTest.cpp` | `journal_revert_rolls_back_child_warm_address` | ✅ | geth journal revert 语义 | 含否定性 post-revert 断言 |
| `Eip2929AccessHostTest.cpp` | `access_account_disabled_when_warm_access_off` | ✅ | flag OFF → 无 warm | 否定路径 |
| `WarmTransactionEntryTest.cpp` | `warms_sender_to_and_coinbase_for_call_transaction` | ✅ | geth `statedb.Prepare` + EIP-3651 | `@ EVMC_SHANGHAI` coinbase warm |
| `WarmTransactionEntryTest.cpp` | `warms_access_list_address_and_storage_keys` | ✅ | EIP-2930 W2 warm | type-1 tx + 2 storage keys |
| `WarmTransactionEntryTest.cpp` | `builds_block_info_with_expected_fields` | 🟡 | BlockInfoBuilder | 非 EIP 专项；smoke |
| `ExecuteViaEthFixtureTest.cpp` | `existing_prague_fixtures_via_execute_via_eth` | — | 见下表 fixture 子项 | 驱动循环；本身无独立断言 |

**显式用例小计：** ✅ 11 | 🟡 5 | 🔴 0

---

## Fixture 子项审计（`ExecuteViaEthFixtureTest` 循环，16）

断言体：`FixtureAssert.h` — status、output、logs；`gas_used==0` 时**跳过 gas 检查**。

| Fixture | 判定 | 金标准 | 备注 |
|---------|------|--------|------|
| `stExample_return42.json` | ✅ | 手工 return-42 | status + output `0x2a`；`gas_used=18` |
| `stRevert_revertBasic.json` | ✅ | revert 语义 | `gas_used=6` |
| `stRevert_revertDepth.json` | ✅ | nested revert | `gas_used=2632` |
| `stBLS_add.json` | ✅ | geth `blsG1Add.json[1]` | output 128B 一致；已补 gas |
| `stCreate2_basic.json` | ✅ | GeneralStateTests CREATE2 | `gas_used` + post-state 断言均已补齐 |
| `stCreate_initCode.json` | ✅ | CREATE | `gas_used` + post-state 断言均已补齐 |
| `stExample_gasPrice0.json` | ✅ | smoke(return42/gasPrice=0) | `gas_used=18` 已落盘 |
| `stCall_emptyAccount.json` | 🟡 | empty account CALL | 实测 gas 为 0；当前机制下 `gas_used=0` 仍为跳过 |
| `stPrecompile_sha256.json` | ✅ | geth `sha256.json` | output + gas 均断言 |
| `stPrecompile_identity.json` | ✅ | identity precompile | output + gas 均断言 |
| `stPrecompile_ecrecover.json` | ✅ | geth `ecRecover.json` | expected output 已修正并补 gas |
| `stModExp_basic.json` | ✅ | geth `modexp.json` | 具名向量 + output + gas |
| `stSelfDestruct_basic.json` | ✅ | GeneralStateTests/stSelfDestruct | `gas_used=7603` + post-state（含 6780 语义） |
| `stEIP7702_delegation.json` | ✅ | GeneralStateTests/stEIP7702 | auth + delegation E2E，含 gas |
| `stExample_return42_warmProps.json` | 🟡 | 原 2930 smoke 重命名 | 仅 warm props smoke，非 type-1 access list E2E |
| `prague_create_empty_initcode.json` | 🟡 | smoke | 空 initcode CREATE 状态为 `EVMC_SUCCESS`；实测 gas 为 0（仍跳过） |

**Fixture 小计：** ✅ 13 | 🟡 3 | 🔴 0

---

## Spot-check：5 个 imported fixture vs geth/Besu 金标准

| Fixture | FB expected | geth/Besu 对照 | 判定 | 说明 |
|---------|-------------|----------------|------|------|
| `stBLS_add.json` | output 128B G1 sum + gas | geth `core/vm/testdata/precompiles/blsG1Add.json` #1 `bls_g1add_(inf+g1=g1)` | ✅ | output 对齐；gas 已补 |
| `stEIP7702_delegation.json` | SUCCESS + output `0x2a` + delegation | GeneralStateTests/stEIP7702 type-4 授权 + delegation code `0xEF0100‖addr` | ✅ | fixture 已含 authorization/delegation，且 gas 已断言 |
| `stSelfDestruct_basic.json` | SUCCESS, gas 7603, output ∅ | Cancun/Prague 6780 语义（转账 + 代码保留） | ✅ | 已补 post-state 检查（含 code_nonempty 路径） |
| `stModExp_basic.json` | SUCCESS + output + gas | geth `modexp.json` 具名向量 | ✅ | 向量、output、gas 均已落地 |
| `stExample_return42_warmProps.json` | SUCCESS + output `0x2a` | 非 geth type-1 access list 向量 | 🟡 | 原 `stEIP2930_accessList` 重命名保留 smoke 属性 |

---

## 跨路径引用（非 `test/eth/` 枚举，Part 3 保留）

| 测试 | 判定 | 备注 |
|------|------|------|
| `Bcos7623PrecheckTest` | 🟡 | FISCO `executeViaHost` |
| `EthTxGasSettlementTest` | ✅ | geth `FloorDataGas` / 27216 |
| `EthTxGasSettlementExecutorTest` | ✅ | canonical 27216 |
| *(gap)* `ExecuteViaEth` 7623 | 🟡 | 无 `bcos-evm/test/eth/*7623*` |
| `Eip7702ApplyAuthorizationTest` | 🟡 | opstack；manual `eip7702=true` |

---

## 汇总

| 类别 | ✅ | 🟡 | 🔴 | 合计 |
|------|----|----|-----|------|
| 显式单元用例 | 11 | 5 | 0 | 16 |
| Fixture 子项 | 13 | 3 | 0 | 16 |
| Spot-check（5 fixture） | 1 | 4 | 0 | 5 |
| **Part 3 全表（含跨路径 5 行）** | **26** | **11** | **0** | **37** |

### 结论

- **🔴 断言：0** — 未发现 literal 期望值与金标准**直接冲突**的用例；假覆盖均为 🟡（断言过弱或未测目标语义）。
- **🟡 断言：11** — 主要风险收敛为：(1) `gas_used=0` 跳过仍剩 2 个（`stCall_emptyAccount`、`prague_create_empty_initcode`）；(2) `stExample_return42_warmProps` 仍是 warm props smoke；(3) 跨路径层面仍有 7623/7702 的非 ETH reference 形态分裂。
- **优先补测：** 若继续收敛 🟡，优先实现显式 `skip_gas_assert` 字段并保留 gas=0 literal；其次补一个真 type-1 access list fixture，替换 warm props smoke。

### Wave 2 FIX-12 交叉引用（2026-06-21）

OPStack inherited Isthmus profile 文档 sign-off 见 `_work/task8-inherited-smoke.md` §Wave 2 FIX-12。本文件（ETH reference 横向断言）无新增 🔴；OP 侧 Part 2 🟡 闭合见 `2026-06-20-opstack-isthmus-audit.md` FIX-13。
