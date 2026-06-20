# Task 10 — OPStack Isthmus 测试用例清单

**范围：** `bcos-evm/test/opstack/*.cpp`、`transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`  
**日期：** 2026-06-20  
**方法：** `rtk grep -n "BOOST_AUTO_TEST_CASE" …`

---

## 汇总

| 指标 | 值 |
|------|-----|
| 测试文件数 | **29**（opstack 28 + executor fixture 1） |
| `BOOST_AUTO_TEST_CASE` 总数 | **65** |
| 断言审计 | 见 `task10-assertions.md` Part 3 |

| 文件 | 用例数 | 关联能力 / 场景 |
|------|--------|-----------------|
| `BlobGasBalanceTest.cpp` | 1 | EIP-4844 blob fee cap vs base fee |
| `CalcRefundTest.cpp` | 3 | `postExecuteGasSettlement` / EIP-7623 floor |
| `CanTransferTest.cpp` | 2 | deposit value transfer / predeploy 转账 |
| `DepositMintTest.cpp` | 1 | deposit mint 前置 |
| `DepositNoFeeRoutingTest.cpp` | 2 | deposit 无 fee 路由；失败 mint+nonce |
| `DepositTxPreCheckTest.cpp` | 6 | deposit precheck / 非 deposit 对照 |
| `Eip7702ApplyAuthorizationTest.cpp` | 1 | EIP-7702 auth 安装 delegation |
| `Eip7702ClearDelegationTest.cpp` | 1 | EIP-7702 清零 delegation |
| `Eip7702DelegationSenderTest.cpp` | 2 | 7702 sender code 形态 precheck |
| `Eip7702PreCheckTest.cpp` | 2 | 7702 auth list on CREATE 拒绝 |
| `EmptyCodeHookTest.cpp` | 1 | L1Block 空 code hook |
| `EvmoneRefundSpikeTest.cpp` | 1 | evmone SSTORE clear refund 语义 |
| `GasFeeCapBalanceTest.cpp` | 1 | gasFeeCap balance 预检 |
| `IsthmusPostExecutionPolicyTest.cpp` | 1 | `makeIsthmusRevisionConfig()` profile |
| `L1AttributesDepositFailureTest.cpp` | 1 | L1 attributes deposit REVERT 不提交 slot |
| `L1AttributesDepositTest.cpp` | 1 | L1 attributes → 后续 user tx L1 fee |
| `L1BlockGetterTest.cpp` | 1 | L1Block getter via `opStackExecuteViaHost` |
| `L1BlockPredeployTest.cpp` | 3 | Isthmus fixture setter/getter / ACL |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | 1 | 7702 auth 经 opstack 编排传播 |
| `OpStackExecuteViaHostSmokeTest.cpp` | 4 | fee 路由 / balance / revert / hard fail |
| `OpStackFeeTest.cpp` | 5 | Fjord L1 + Isthmus operator fee 单元 |
| `OpStackFloorGasTest.cpp` | 9 | EIP-7623 `floorDataGas` + entry check |
| `OpStackSettlementTest.cpp` | 2 | buyGas/refundGas 全路由 + hard fail |
| `OpStackTxInputBuilderTest.cpp` | 2 | deposit / 7702 extra bytes 解码 |
| `OpStackTxPropsTest.cpp` | 1 | tx-entry warm destination |
| `RefundIsthmusTest.cpp` | 1 | Isthmus operator refund |
| `RollupCostTest.cpp` | 2 | FLZ / rollup cost data vs op-geth |
| `TestOpStackTransactionExecutorFixture.cpp` | 7 | TE E2E smoke（fee / deposit / hard fail） |
| **合计** | **65** | |

---

## `bcos-evm/test/opstack/*.cpp`

### `BlobGasBalanceTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `blob_gas_fee_cap_under_blob_base_fee_is_rejected` | `InsufficientFunds` when `blobGasFeeCap < blobBaseFee` |

### `CalcRefundTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `Settlement_capBinds` | refund/gasUsed/gasRemaining/maxUsedGas 字面量 |
| 2 | `Settlement_floorDataGasBumpsGasUsed` | floor=700 时 gasUsed=700 |
| 3 | `EvmoneParity_noDoubleCount` | evmone gas_left 不 double-count refund |

### `CanTransferTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `value_transfer_rejected_when_sender_balance_insufficient` | deposit value 不足 → `InsufficientFunds` |
| 2 | `transfer_to_predeploy_allowed_if_funded` | 向 predeploy 转账 status 非 NotEnoughCash/InsufficientFunds |

### `DepositMintTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `deposit_mint_is_applied_before_execution` | SUCCESS；sender balance=100 |

### `DepositNoFeeRoutingTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `deposit_skips_fee_routing_recipients` | base/L1/operator/coinbase balance=0 |
| 2 | `deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce` | REVERT；mint=100；nonce 7→8；**`gasUsed=50'000`（gasLimit）** |

### `DepositTxPreCheckTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `system_deposit_is_rejected` | system tx → `Malformed` |
| 2 | `deposit_skips_nonce_and_fee_checks_but_still_subtracts_gas_pool` | 错误 nonce/fee 仍通过；gas pool hook |
| 3 | `non_deposit_rejects_nonce_mismatch` | `NonceCheckFail` |
| 4 | `non_deposit_rejects_invalid_eip1559_caps` | tip>feeCap → `Malformed` |
| 5 | `non_deposit_rejects_blob_fee_cap_under_base_fee` | blob cap<base → `InsufficientFunds` |
| 6 | `non_deposit_rejects_auth_list_on_create` | CREATE+auth → `Malformed` |

### `Eip7702ApplyAuthorizationTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `valid_auth_installs_delegation_invalid_is_ignored_and_refund_added` | `0xEF0100‖addr` code 23B；nonce→1 |

### `Eip7702ClearDelegationTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `auth_with_zero_target_clears_existing_delegation_code` | code 清空；nonce→1 |

### `Eip7702DelegationSenderTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `sender_with_delegation_code_passes_precheck` | 无 error |
| 2 | `sender_with_non_delegation_code_is_rejected` | `Malformed` |

### `Eip7702PreCheckTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `rejects_authorization_list_on_create` | CREATE+auth → `Malformed` |
| 2 | `rejects_explicit_empty_authorization_list` | 空 auth list → `Malformed` |

### `EmptyCodeHookTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `top_level_call_hits_chain_precompile_hook_on_empty_code` | L1Block setter selector → `EVMC_REVERT` |

### `EvmoneRefundSpikeTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `SstoreClear_recordsGasLeftAndRefund` | refund=4800；`DEFERRED_REFUND` 语义 |

### `GasFeeCapBalanceTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `gas_fee_cap_balance_check_rejects_insufficient_sender` | balance 不足 → `NotEnoughCash` |

### `IsthmusPostExecutionPolicyTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `isthmus_revision_config_disables_prague_post_execution` | `eip7702`/`eip7623` true；`prague_post_execution` false |

### `L1AttributesDepositFailureTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `failed_l1_attributes_deposit_does_not_commit_slot_changes` | 无效 calldata REVERT；stateDiff 无 L1Block slot |

### `L1AttributesDepositTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `l1_attributes_deposit_updates_l1block_and_affects_following_user_tx` | deposit SUCCESS；后续 user tx L1 recipient balance>0 |

### `L1BlockGetterTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `op_host_extension_dispatches_l1block_getter` | `l1BaseFee()` output = u256(123456) |

### `L1BlockPredeployTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `setter_unpacks_isthmus_fixture_into_slots` | `isthmus_l1_attributes.bin` → slot 字节字面量 |
| 2 | `setter_rejects_non_depositor_sender` | 非 depositor → REVERT；slot 仍零 |
| 3 | `getters_return_slot_values_after_setter` | 5 个 getter selector 返回值 vs fixture |

### `OpStack7702ExecuteViaHostPropagationTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `opStackExecuteViaHost_propagates_authorizations_to_executeMessage` | SUCCESS；delegation code 安装 |

### `OpStackExecuteViaHostSmokeTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `l1_fee_recipient_gets_fee_on_success` | L1=50 literal；base/coinbase/operator `>0`；receipt l1Fee/operatorFee |
| 2 | `insufficient_balance_fails_before_execution` | `EVMC_INSUFFICIENT_BALANCE`；L1=0 |
| 3 | `revert_refunds_unused_gas_and_keeps_l1_fee` | REVERT；L1=50；sender 部分退款 |
| 4 | `hard_failure_still_refunds_unused_gas_and_routes_fees` | 非 SUCCESS；sender/L1 `>0` |

### `OpStackFeeTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `FjordL1_emptyTx_matches3203000` | `empty_tx.bin` → L1 cost 3'203'000 |
| 2 | `IsthmusOperator_gas1618_matchesFixture` | operator cost = 1256417826611659930 |
| 3 | `FjordL1_emptyRollupCostData_returnsZero` | 空 rollup → L1=0 |
| 4 | `IsthmusOperator_zeroParams_returnsZero` | 零参数 → operator=0 |
| 5 | `LoadOpStackFeeParams_unpacksSlots` | slot 1/2/3/8 解码 vs pack |

### `OpStackFloorGasTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `FloorDataGas_emptyCalldata_is21000` | floor=21000 |
| 2 | `FloorDataGas_zeroBytesOnly` | 100 zero bytes |
| 3 | `FloorDataGas_nonZeroBytesOnly` | 100×0xff |
| 4 | `FloorDataGas_mixedZeroAndNonZero` | 50/50 mix |
| 5 | `FloorDataGas_singleZeroByte` | 1 zero |
| 6 | `FloorDataGas_singleNonZeroByte` | 1 non-zero |
| 7 | `FloorDataGas_overflow_returnsError` | `GasUintOverflow` |
| 8 | `ExecuteEntryFloorCheck_gasLimitBelowFloor_rejects` | below floor → reject |
| 9 | `ExecuteEntryFloorCheck_gasLimitAtFloor_accepts` | at floor → ok |

### `OpStackSettlementTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `Settlement_routesCoinbaseBaseFeeL1AndOperator` | coinbase/base/L1/operator/sender 余额链 |
| 2 | `HardFailure_stillRefundsUnusedGas` | OOG hard fail 仍路由 L1/operator |

### `OpStackTxInputBuilderTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `decodes_deposit_extra_transaction_bytes` | deposit RLP 字段 gas/mint/value/to/from |
| 2 | `decodes_eip7702_authorization_from_extra_bytes` | type-4 auth list nonce/address/authority |

### `OpStackTxPropsTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `applyDefaultTxProps_sets_warm_destination_from_kind` | CALL warm；CREATE 不 warm |

### `RefundIsthmusTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `RefundIsthmus_refundsLimitMinusUsedCost` | limit 2618 − used 1500 → sender +1118 |

### `RollupCostTest.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `FlzCompressLen_matchesOpGethVectors` | FLZ len 0/21/31/202 vs op-geth |
| 2 | `NewRollupCostData_countsBytesAndFastLz` | empty tx ones=30, fastLz=31 |

---

## `transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`

| # | 用例名 | 关键断言 |
|---|--------|----------|
| 1 | `l1_fee_recipient_gets_fee_on_success` | status=0；receipt l1Fee≠0x0；L1 recipient>0 |
| 2 | `insufficient_balance_fails_before_execution` | `NotEnoughCash`；L1=0 |
| 3 | `revert_keeps_l1_fee` | 非 0 status；l1Fee≠0x0；sender 部分退款 |
| 4 | `deposit_mint_applied_without_fee_routing` | deposit SUCCESS；sender=100；L1=0 |
| 5 | `deposit_skips_fee_routing_recipients` | base/L1/operator=0 |
| 6 | `deposit_failure_reverts_but_keeps_mint` | 非 0 status；mint=100；nonce 7→8 |
| 7 | `hard_failure_status_propagates_without_state_commit` | INVALID 不 commit；receipt l1Fee≠0；storage 不变 |

---

## 共享模式 / 审计锚点

| 模式 | 出现文件 | 说明 |
|------|----------|------|
| **手动 `m_isIsthmus = true`** | `CanTransferTest`, `DepositMintTest`, `DepositNoFeeRoutingTest`, `L1AttributesDepositTest`, `OpStack7702ExecuteViaHostPropagationTest`, `OpStackExecuteViaHostSmokeTest`, `OpStackSettlementTest`, `RefundIsthmusTest` | 8 文件；生产 `OpStackTransactionExecutorImpl` 未设 flag（Task 1/3） |
| **deposit 失败 `gasUsed=gasLimit`** | `DepositNoFeeRoutingTest::deposit_failure_*` | 编码 FB 偏离；op-geth Regolith+ REVERT 应 actual gas（Task 4 D4-2） |
| **无 E2E operator fee** | `TestOpStackTransactionExecutorFixture` 全部 7 例 | 无 `operatorFee` receipt / `OP_OPERATOR_FEE_RECIPIENT` 断言 |
| **operator fee 弱断言** | `OpStackExecuteViaHostSmokeTest::l1_fee_recipient_gets_fee_on_success` | L1=50 literal ✅；operator 仅 `>0` |

---

## 范围外（交叉引用）

| 文件 | 说明 |
|------|------|
| `task8-assertion-audit.md` | ETH reference `test/eth/` |
| `task3-operator-fee.md` | operator fee 公式 / wiring 深审 |
| `task4-deposit.md` | deposit 失败 gas 语义 |
