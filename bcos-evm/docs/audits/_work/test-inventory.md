# Task 8 — ETH Reference 测试用例清单

**范围：** `bcos-evm/test/eth/*.cpp`、`Eip2929AccessHostTest.cpp`、`WarmTransactionEntryTest.cpp`  
**日期：** 2026-06-20  
**方法：** `rtk grep -n "BOOST_AUTO_TEST_CASE" …`

---

## 汇总

| 文件 | 用例数 | 关联 EIP / 场景 |
|------|--------|-----------------|
| `RevisionConfigProfileTest.cpp` | 4 | RevisionConfig profile / EthPolicy 分叉阶梯 |
| `ExecuteViaEthFixtureTest.cpp` | 1（驱动 21 个 JSON fixture） | 端到端 `executeViaEth` |
| `EthTxInputBuilderTest.cpp` | 1 | EIP-7702 type-4 tx 字段解码 |
| `FiscoTxInputBuilderTest.cpp` | 1 | 同上（FISCO `executeViaHost` 路径） |
| `Eip2537KernelTest.cpp` | 1 | EIP-2537 G1Add @0x0b |
| `TxFeaturePrepareTest.cpp` | 1 | EIP-2929 tx-entry destination warm |
| `Eip2929AccessHostTest.cpp` | 4 | EIP-2929 runtime COLD/WARM + journal revert |
| `WarmTransactionEntryTest.cpp` | 3 | EIP-2929/3651 tx-entry warm + BlockInfoBuilder |
| **合计** | **16 显式用例 + 21 fixture 子项** | |

---

## `bcos-evm/test/eth/*.cpp`

### `RevisionConfigProfileTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `revision_config_bool_field_macro_count` | `revisionConfigBoolFieldCount() == 13` |
| 2 | `eth_policy_full_fork_snapshots` | 5 个区块高度 × 全字段 `BOOST_CHECK_EQUAL` vs `ExpectedRevisionConfig` |
| 3 | `fisco_policy_feature_gate_snapshots` | 4 组 `Features` × FiscoPolicy.eth() 快照 |
| 4 | `isthmus_helper_sparse_profile_all_fields` | `makeIsthmusRevisionConfig()` vs 期望（含 `eip7702=true`） |

### `ExecuteViaEthFixtureTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `existing_prague_fixtures_via_execute_via_eth` | 遍历 `fixtures/state/` + `imported/` 全部 JSON；每 fixture 调用 `assertFixtureResult` |

**Fixture 子项（21 个，由上述用例循环驱动）：**

| Fixture | 来源目录 |
|---------|----------|
| `prague_call_empty_account.json` | `fixtures/state/` |
| `prague_call_return_word.json` | `fixtures/state/` |
| `prague_call_revert.json` | `fixtures/state/` |
| `prague_create_empty_initcode.json` | `fixtures/state/` |
| `prague_selfdestruct.json` | `fixtures/state/` |
| `stBLS_add.json` | `imported/` |
| `stCall_emptyAccount.json` | `imported/` |
| `stCreate2_basic.json` | `imported/` |
| `stCreate_initCode.json` | `imported/` |
| `stEIP2930_accessList.json` | `imported/` |
| `stEIP7702_delegation.json` | `imported/` |
| `stExample_gasPrice0.json` | `imported/` |
| `stExample_return42.json` | `imported/` |
| `stModExp_basic.json` | `imported/` |
| `stPrecompile_ecrecover.json` | `imported/` |
| `stPrecompile_identity.json` | `imported/` |
| `stPrecompile_sha256.json` | `imported/` |
| `stRevert_revertBasic.json` | `imported/` |
| `stRevert_revertDepth.json` | `imported/` |
| `stSelfDestruct_basic.json` | `imported/` |

### `EthTxInputBuilderTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `fillWeb3Fields_maps_eip7702_authorizations` | `web3TypedTxKind==0x04`；auth list size/nonce/address/authority 非零 |

### `FiscoTxInputBuilderTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `fillWeb3Fields_maps_eip7702_authorizations` | 同 Eth 版，目标 `ExecuteViaHostInput` |

### `Eip2537KernelTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `stBLS_add_precompile_0x0b_via_executeMessage` | `EVMC_SUCCESS`；output bytes == fixture |

### `TxFeaturePrepareTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `setWarmDestinationFromKind_matches_create_vs_call` | CALL→`warmDestination=true`；CREATE/CREATE2→false |

---

## `bcos-evm/test/state/`

### `Eip2929AccessHostTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `access_account_cold_then_warm` | 首次 `EVMC_ACCESS_COLD`，二次 `EVMC_ACCESS_WARM` |
| 2 | `access_storage_cold_then_warm` | 同上（storage） |
| 3 | `journal_revert_rolls_back_child_warm_address` | checkpoint/revert 后 `!is_address_warm` |
| 4 | `access_account_disabled_when_warm_access_off` | `warmAccessEnabled=false` 时恒 COLD |

### `WarmTransactionEntryTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `warms_sender_to_and_coinbase_for_call_transaction` | `is_address_warm(from/to/coinbase)` @ SHANGHAI |
| 2 | `warms_access_list_address_and_storage_keys` | access list 地址 + 2 storage key warm |
| 3 | `builds_block_info_with_expected_fields` | BlockInfoBuilder 字段 literal |

---

## 共享断言 / 适配层

| 文件 | 职责 |
|------|------|
| `FixtureAssert.h` | `assertFixtureResult`：status、output bytes、logs count；`gas_used!=0` 时才查 gas |
| `EthFixtureAdapter.h` | `makePragueRevisionConfig()`（**无 `eip7702`**）；`buildExecuteViaEthInput` 不填 auth list |

---

## 范围外（Part 3 交叉引用，非本 inventory 枚举）

| 文件 | 说明 |
|------|------|
| `Bcos7623PrecheckTest.cpp` | FISCO `executeViaHost` |
| `transaction-executor/tests/EthTxGasSettlementTest.cpp` | TE settlement / 27216 canonical |
| `transaction-executor/tests/EthTxGasSettlementExecutorTest.cpp` | TE e2e floor receipt |
