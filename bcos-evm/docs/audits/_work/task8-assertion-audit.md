# Task 8 — 测试断言审计（横向）

**日期：** 2026-06-20  
**Skill：** `fisco-evm-test-coverage/references/assertion-audit.md`  
**清单：** `_work/test-inventory.md`  
**金标准：** geth v1.17.3、`core/vm/testdata/precompiles/*.json`；Besu 26.6.0 referencetests（抽样）

---

## 断言等级说明

| 符号 | 含义 |
|------|------|
| ✅ | 期望值与金标准 / EthPolicy / EIP MUST 一致；路径有效 |
| 🟡 | 方向对但偏弱：helper-only、缺 post-state/gas、名实不符 smoke |
| 🔴 | 期望值错误、测错 API、revision 组合使断言无约束力 |

---

## 显式单元用例审计（16）

| 测试文件 | 用例 | 判定 | 金标准来源 | 备注 |
|----------|------|------|------------|------|
| `RevisionConfigProfileTest.cpp` | `revision_config_bool_field_macro_count` | ✅ | `RevisionConfig.h` 宏 | 结构完整性 |
| `RevisionConfigProfileTest.cpp` | `eth_policy_full_fork_snapshots` | ✅ | `EthPolicy.h:27-41` | 5 分叉 × 全字段；PRAGUE 无 `eip7702` 与源码一致 |
| `RevisionConfigProfileTest.cpp` | `fisco_policy_feature_gate_snapshots` | 🟡 | `FiscoPolicy.h` | 测 FiscoPolicy 非 EthPolicy baseline；PRAGUE 行 `eip7702=true` 记录路径分裂 |
| `RevisionConfigProfileTest.cpp` | `isthmus_helper_sparse_profile_all_fields` | 🟡 | `makeIsthmusRevisionConfig()` | OPStack helper；`eip7702=true` 与 EthPolicy 不同 |
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

## Fixture 子项审计（`ExecuteViaEthFixtureTest` 循环，21）

断言体：`FixtureAssert.h` — status、output、logs；`gas_used==0` 时**跳过 gas 检查**。

| Fixture | 判定 | 金标准 | 备注 |
|---------|------|--------|------|
| `stExample_return42.json` | ✅ | 手工 return-42 | status + output `0x2a`；`gas_used=18` 有断言 |
| `stRevert_revertBasic.json` | ✅ | revert 语义 | `gas_used=6` |
| `stRevert_revertDepth.json` | ✅ | nested revert | `gas_used=2632` |
| `stBLS_add.json` | ✅ | geth `blsG1Add.json[1]` | output 128B 一致；gas 未断言（0） |
| `stCreate2_basic.json` | 🟡 | GeneralStateTests CREATE2 | 仅 `gas_used=32030`；无 created address |
| `stCreate_initCode.json` | 🟡 | CREATE | output 32B code hash；无 post-state |
| `stExample_gasPrice0.json` | 🟡 | smoke | return42 变体；gas 跳过 |
| `stCall_emptyAccount.json` | 🟡 | empty account CALL | gas 跳过；无 balance 断言 |
| `stPrecompile_sha256.json` | 🟡 | geth `sha256.json` | output 32B 有断言；gas 跳过 |
| `stPrecompile_identity.json` | 🟡 | identity precompile | output 4B；gas 跳过 |
| `stPrecompile_ecrecover.json` | 🟡 | geth `ecRecover.json` | `to=0x01` 但 expected output 空；未对照 JSON 向量 |
| `stModExp_basic.json` | 🟡 | geth `modexp.json` | 106B 全零长度输入；output 空；gas 跳过 |
| `stSelfDestruct_basic.json` | 🟡 | GeneralStateTests/stSelfDestruct | `gas_used=7603`；**无** 0xbb 余额/post-state（6780 🔴 实现缺口仍 PASS） |
| `prague_selfdestruct.json` | 🟡 | 同上 duplicate | 与 imported 同 bytecode/gas |
| `stEIP7702_delegation.json` | 🟡 | GeneralStateTests/stEIP7702 | **假覆盖**：plain CALL return42；无 auth list / delegation code |
| `stEIP2930_accessList.json` | 🟡 | GeneralStateTests/stEIP2930 | **名实不符**：无 JSON access_list；仅 `tx_props` warm + return42 |
| `prague_call_return_word.json` | 🟡 | smoke | gas 18；弱 |
| `prague_call_revert.json` | 🟡 | smoke | gas 6 |
| `prague_call_empty_account.json` | 🟡 | smoke | gas 跳过 |
| `prague_create_empty_initcode.json` | 🟡 | smoke | gas 跳过 |

**Fixture 小计：** ✅ 4 | 🟡 16 | 🔴 0

---

## Spot-check：5 个 imported fixture vs geth/Besu 金标准

| Fixture | FB expected | geth/Besu 对照 | 判定 | 说明 |
|---------|-------------|----------------|------|------|
| `stBLS_add.json` | output 128B G1 sum | geth `core/vm/testdata/precompiles/blsG1Add.json` #1 `bls_g1add_(inf+g1=g1)` — **Expected 逐字节一致** | ✅ | Input 前缀一致；gas 未在 fixture 断言 |
| `stEIP7702_delegation.json` | SUCCESS + output `0x2a` | GeneralStateTests/stEIP7702 需 type-4 tx + delegation code `0xEF0100‖addr` | 🟡 | FB pre[0xbb] 为 `PUSH32 42 RETURN` 字节码；无 authorization；`makePragueRevisionConfig` 无 `eip7702` |
| `stSelfDestruct_basic.json` | SUCCESS, gas 7603, output ∅ | bytecode `PUSH20 0xbb SELFDESTRUCT` 与 Cancun/Prague 语义一致 | 🟡 | gas 7603 合理（2929 cold/warm）；**无 post-state**（0x12→0xbb 转账、6780 保留代码）— stub Host 仍 PASS |
| `stModExp_basic.json` | SUCCESS, output ∅ | geth `modexp.json` 无完全匹配 106B 输入；零长度 modexp 边界 case | 🟡 | hand-crafted；`gas_used=0` 跳过 gas；output 空未独立验证 |
| `stEIP2930_accessList.json` | SUCCESS + output `0x2a` | geth EIP-2930 需 tx accessList 字段影响 gas | 🟡 | JSON 无 `access_list`；loader 不支持；仅 preset `tx_props.warm_*` — 非 2930 E2E |

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
| Fixture 子项 | 4 | 16 | 0 | 20 |
| Spot-check（5 fixture） | 1 | 4 | 0 | 5 |
| **Part 3 全表（含跨路径 5 行）** | **16** | **25** | **0** | **41** |

### 结论

- **🔴 断言：0** — 未发现 literal 期望值与金标准**直接冲突**的用例；假覆盖均为 🟡（断言过弱或未测目标语义）。
- **🟡 断言：25** — 主要风险：(1) fixture `gas_used=0` 跳过 gas；(2) 6780/7702/2930 名实不符 smoke；(3) FISCO/OPStack 路径重复或分裂；(4) 无 ETH reference 7623/7702 E2E。
- **优先补测：** 真 7702 fixture + `eip7702=true` profile；6780 post-state；2930 带 access_list JSON；7623 `ExecuteViaEth` canonical 27216；预编译 gas 断言。
