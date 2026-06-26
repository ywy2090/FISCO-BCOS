# Task 10 — OPStack Isthmus 测试断言审计

**初审计日期：** 2026-06-20  
**复审计 commit：** `54e17a62c`（2026-06-21）  
**Wave 2 sign-off：** 2026-06-21（96 用例；88✅ / 8🟡 / 0🔴）  
**Skill：** `fisco-evm-test-coverage/references/assertion-audit.md`  
**清单：** `_work/test-inventory-opstack.md`  
**金标准：** op-geth v1.101702.2 @ `e8800cffe`；optimism-specs `689a96f` Isthmus/deposit 章节  
**交叉引用：** Task 1 wiring、Task 4 deposit、Task 8 ETH reference、OP-01–OP-15 remediation

---

## 断言等级说明

| 符号 | 含义 |
|------|------|
| ✅ | 期望值与 op-geth / optimism specs / FB 单元向量一致；路径有效 |
| 🟡 | 方向对但偏弱：smoke、`>0` 代替 literal、缺 post-state/gas/nonce、生产 E2E 覆盖不全 |
| 🔴 | 期望值编码实现偏离；或断言与金标准直接冲突 |

---

## 重点发现（复审计 @ `54e17a62c`）

### 1. 手动 `m_isIsthmus = true` — **已清理至 3 处（有意保留）**

| 文件 | 行 | 说明 |
|------|-----|------|
| `OpStackSettlementTest.cpp` | `:33`, `:75` | 直接测 `OpStackTxExecutor::buyGas/refundGas`，非 `opStackExecuteViaHost` |
| `RefundIsthmusTest.cpp` | `:30` | 直接测 `refundIsthmusOperatorCost` 单元 |

**初审计 stale：**「8 文件手动 `m_isIsthmus`」— OP-01/OP-15 后 smoke/E2E 改经 `makeIsthmusRevisionConfig` + `isIsthmusOrchestrationProfile` 自动启用；**生产** `OpStackTransactionExecutorImpl.h:210-211` 亦硬编码 `m_isIsthmus=true`。

### 2. `DepositNoFeeRoutingTest` — gasLimit 断言 **已修正 ✅**

`deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce`（`:127-128`）：

```cpp
BOOST_CHECK_EQUAL(output.gasUsed, 21'000);
BOOST_CHECK_LT(output.gasUsed, 50'000);
```

- **OP-04 闭合：** REVERT → actual metered gas（非 gasLimit）。
- **新增** `deposit_entry_failure_bumps_nonce_and_uses_gas_limit` — entry OOG → gasLimit（OP-05）。

**初审计 stale：** 🔴 `gasUsed=50'000` — **已删除**。

### 3. Operator fee E2E — **已补测 ✅**

`TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success` — `receipt->operatorFee()` + `OP_OPERATOR_FEE_RECIPIENT` balance `>0`。

`L1AttributesDepositTest` — L1/operator **literal** vs `l1CostFjord` / `operatorCostIsthmus` 公式。

**残余 🟡：** `OpStackExecuteViaHostSmokeTest` operator 仍 `>0`（L1=50 literal ✅）。

---

## Part 3 — 全表断言审计（83 opstack + 13 TE = **96 用例**）

> 初审计 65 用例；remediation + Wave 2 新增 31 用例。完整清单见 `test-inventory-opstack.md`。

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `BlobGasBalanceTest.cpp` | `blob_hashes_without_blob_gas_fee_cap_is_rejected` | ✅ | EIP-4844 preCheck | FIX-09 |
| `BlobGasBalanceTest.cpp` | `blob_hashes_rejected_when_eip4844_disabled` | ✅ | profile gate | FIX-09 |
| `BlobGasBalanceTest.cpp` | `blob_gas_fee_cap_under_blob_base_fee_is_rejected` | ✅ | EIP-4844 blob gas price check | `InsufficientFunds` |
| `BlobGasBalanceTest.cpp` | `buy_gas_deducts_blob_base_fee_times_blob_gas` | ✅ | blob buyGas 扣款 | OP-06 |
| `BlobGasBalanceTest.cpp` | `buy_gas_rejects_insufficient_balance_for_blob_cost` | ✅ | blob balance check | OP-06 |
| `BlobGasBalanceTest.cpp` | `opStackExecuteViaHost_deducts_blob_fee_on_success` | ✅ | blob fee 路由 delta | FIX-09 |
| `CalcRefundTest.cpp` | `Settlement_capBinds` | ✅ | EIP-3529 cap + settlement | 字面量 |
| `CalcRefundTest.cpp` | `Settlement_floorDataGasBumpsGasUsed` | ✅ | EIP-7623 floor | gasUsed=700 |
| `CalcRefundTest.cpp` | `EvmoneParity_noDoubleCount` | ✅ | evmone gas_left | 防 double-count |
| `CanTransferTest.cpp` | `value_transfer_rejected_when_sender_balance_insufficient` | ✅ | deposit value 转账 | `InsufficientFunds` |
| `CanTransferTest.cpp` | `transfer_to_predeploy_allowed_if_funded` | 🟡 | predeploy 可接收 value | 仅否定性 status |
| `DepositMintTest.cpp` | `deposit_mint_is_applied_before_execution` | ✅ | mint + nonce + depositNonce | OP-03 |
| `DepositNoFeeRoutingTest.cpp` | `deposit_skips_fee_routing_recipients` | ✅ | deposit 无 fee 路由 | 四 recipient=0 |
| `DepositNoFeeRoutingTest.cpp` | `deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce` | ✅ | Regolith+ REVERT → **actual gas** | **原 🔴 已修正**；gasUsed=21000 |
| `DepositNoFeeRoutingTest.cpp` | `deposit_entry_failure_bumps_nonce_and_uses_gas_limit` | ✅ | entry OOG → gasLimit | OP-05 |
| `DepositTxPreCheckTest.cpp` | `system_deposit_is_rejected` | ✅ | system → Malformed | |
| `DepositTxPreCheckTest.cpp` | `deposit_skips_nonce_and_fee_checks_but_still_subtracts_gas_pool` | ✅ | deposit 跳过 nonce/fee | |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_nonce_mismatch` | ✅ | nonce check | |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_invalid_eip1559_caps` | ✅ | tip≤feeCap | |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_blob_fee_cap_under_base_fee` | ✅ | blob cap | |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_blob_fields_when_eip4844_disabled` | ✅ | eip4844 gate | FIX-09 |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_auth_list_on_create` | ✅ | 7702 on CREATE | |
| `Eip7702ApplyAuthorizationTest.cpp` | `valid_auth_installs_delegation_invalid_is_ignored_and_refund_added` | ✅ | EIP-7702 delegation | |
| `Eip7702ClearDelegationTest.cpp` | `auth_with_zero_target_clears_existing_delegation_code` | ✅ | EIP-7702 clear | |
| `Eip7702DelegationSenderTest.cpp` | `sender_with_delegation_code_passes_precheck` | ✅ | 7702 sender 形态 | |
| `Eip7702DelegationSenderTest.cpp` | `sender_with_non_delegation_code_is_rejected` | ✅ | 非 delegation code | |
| `Eip7702PreCheckTest.cpp` | `rejects_authorization_list_on_create` | ✅ | CREATE+auth | |
| `Eip7702PreCheckTest.cpp` | `rejects_explicit_empty_authorization_list` | ✅ | 空 auth list | |
| `EmptyCodeHookTest.cpp` | `top_level_call_hits_chain_precompile_hook_on_empty_code` | 🟡 | L1Block hook | 仅 REVERT |
| `EvmoneRefundSpikeTest.cpp` | `SstoreClear_recordsGasLeftAndRefund` | ✅ | EIP-3529 SSTORE clear | |
| `GasFeeCapBalanceTest.cpp` | `gas_fee_cap_balance_check_rejects_insufficient_sender` | ✅ | buyGas balance | |
| `IsthmusPostExecutionPolicyTest.cpp` | `isthmus_revision_config_is_prague_tx_level` | ✅ | profile + source scan | |
| `IsthmusPostExecutionPolicyTest.cpp` | `opstack_sources_have_no_prague_block_post_execution_hooks` | ✅ | forbidden symbol scan | |
| `L1AttributesDepositFailureTest.cpp` | `failed_l1_attributes_deposit_does_not_commit_slot_changes` | ✅ | REVERT 不 commit | |
| `L1AttributesDepositTest.cpp` | `l1_attributes_deposit_updates_l1block_and_affects_following_user_tx` | ✅ | L1Block + Fjord/operator literal + depositNonce | OP-12；FIX-06 交叉 |
| `L1BlockGetterTest.cpp` | `op_host_extension_dispatches_l1block_getter` | ✅ | L1Block getter | |
| `L1BlockGetterTest.cpp` | `op_host_extension_dispatches_basefee_getter` | ✅ | legacy alias | OP-14 |
| `L1BlockGetterTest.cpp` | `op_host_extension_dispatches_number_getter` | ✅ | block number getter | OP-14 |
| `L1BlockPredeployTest.cpp` | `setter_unpacks_isthmus_fixture_into_slots` | ✅ | fixture slot 字节 | |
| `L1BlockPredeployTest.cpp` | `setter_rejects_non_depositor_sender` | ✅ | depositor ACL | |
| `L1BlockPredeployTest.cpp` | `getters_return_slot_values_after_setter` | ✅ | 5× getter literal | |
| `L1BlockPredeployTest.cpp` | `pure_getters_match_l1block_constants` | ✅ | IL1Block view/pure | OP-14 |
| `L1BlockPredeployTest.cpp` | `isFeatureEnabled_returns_false_by_default` | ✅ | feature map | OP-14 |
| `OpStack67802537KernelSmokeTest.cpp` | `opStackExecuteViaHost_g1msm_k2_gas_matches_geth_isthmus` | ✅ | geth MSM k=2 → 22776 | OP-08 |
| `OpStack67802537KernelSmokeTest.cpp` | `opStackExecuteViaHost_created_in_tx_selfdestruct_clears_code_isthmus` | ✅ | EIP-6780 same-tx | OP-08 |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | `opStackExecuteViaHost_propagates_authorizations_to_executeMessage` | ✅ | 7702 via opstack | |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | `opStackExecuteViaHost_rejects_7702_intrinsic_below_25000_per_tuple` | ✅ | intrinsic floor | OP-07 |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | `opStackExecuteViaHost_charges_7702_intrinsic_25000_per_tuple` | ✅ | 25000×n debit | OP-07 |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | `opStackExecuteViaHost_refunds_existence_cost_when_authority_already_exists` | ✅ | EIP-7702 existence refund 12500 | FIX-11 / ADR-013 |
| `OpStackExecuteViaHostSmokeTest.cpp` | `l1_fee_recipient_gets_fee_on_success` | 🟡 | L1=50 literal ✅；operator `>0` | auto Isthmus profile |
| `OpStackExecuteViaHostSmokeTest.cpp` | `insufficient_balance_fails_before_execution` | 🟡 | balance pre-check | 无 operator worst-case literal |
| `OpStackExecuteViaHostSmokeTest.cpp` | `revert_refunds_unused_gas_and_keeps_l1_fee` | 🟡 | revert + L1=50 | 无 operator literal |
| `OpStackExecuteViaHostSmokeTest.cpp` | `hard_failure_still_refunds_unused_gas_and_routes_fees` | 🟡 | hard fail | 仅 `>0` |
| `OpStackFeeTest.cpp` | `FjordL1_emptyTx_matches3203000` | ✅ | op-geth Fjord | |
| `OpStackFeeTest.cpp` | `IsthmusOperator_gas1618_matchesFixture` | ✅ | rollup_cost_test.go | |
| `OpStackFeeTest.cpp` | `FjordL1_emptyRollupCostData_returnsZero` | ✅ | 空 rollup | |
| `OpStackFeeTest.cpp` | `IsthmusOperator_zeroParams_returnsZero` | ✅ | 零 operator | |
| `OpStackFeeTest.cpp` | `LoadOpStackFeeParams_unpacksSlots` | ✅ | slot 8 子域 | |
| `OpStackFeeTest.cpp` | `FjordL1_minimumBounds_clampsBelowMinTxSize` | ✅ | min fee bound | OP-11 |
| `OpStackFeeTest.cpp` | `FjordL1_minimumBounds_fastLz171_exceedsMinFee` | ✅ | min bound edge | OP-11 |
| `OpStackFeeTest.cpp` | `FjordL1_contractCallShape_fastLz202_matchesFormula` | ✅ | op-geth vector | OP-11 |
| `OpStackFeeTest.cpp` | `FIX04_FjordL1CostSolidityParity_matchesOpGeth` | ✅ | op-geth 105484/2463 | FIX-04 / ADR-012 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_emptyCalldata_is21000` | ✅ | EIP-7623 floor | |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_zeroBytesOnly` | ✅ | floor token | |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_nonZeroBytesOnly` | ✅ | 4× token | |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_mixedZeroAndNonZero` | ✅ | mixed | |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_singleZeroByte` | ✅ | 边界 | |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_singleNonZeroByte` | ✅ | 边界 | |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_overflow_returnsError` | ✅ | overflow | |
| `OpStackFloorGasTest.cpp` | `ExecuteEntryFloorCheck_gasLimitBelowFloor_rejects` | ✅ | below floor | |
| `OpStackFloorGasTest.cpp` | `ExecuteEntryFloorCheck_gasLimitAtFloor_accepts` | ✅ | at floor | |
| `OpStackSettlementTest.cpp` | `Settlement_routesCoinbaseBaseFeeL1AndOperator` | ✅ | 全路由字面量 | 直接 `m_isIsthmus` 单元测 |
| `OpStackSettlementTest.cpp` | `HardFailure_stillRefundsUnusedGas` | ✅ | OOG settlement | 同上 |
| `OpStackTxInputBuilderTest.cpp` | `decodes_deposit_extra_transaction_bytes` | ✅ | deposit RLP | |
| `OpStackTxInputBuilderTest.cpp` | `decodes_eip7702_authorization_from_extra_bytes` | ✅ | type-4 RLP | |
| `OpStackTxInputBuilderTest.cpp` | `buildRollupCostData_uses_signed_web3_rlp_not_encodeForSign` | ✅ | signed tx bytes | OP-02 |
| `OpStackTxInputBuilderTest.cpp` | `buildRollupCostData_deposit_uses_extra_bytes_unchanged` | ✅ | deposit rollup | OP-02 |
| `OpStackTxInputBuilderTest.cpp` | `decodes_eip4844_blob_fields_from_extra_bytes` | ✅ | type-0x03 RLP | FIX-09 |
| `OpStackTxPropsTest.cpp` | `applyDefaultTxProps_sets_warm_destination_from_kind` | ✅ | EIP-2929 tx-entry | |
| `OpStackTxPropsTest.cpp` | `executor_build_order_clears_warm_destination_for_create` | ✅ | CREATE 不 warm | OP-09 |
| `OpStackTxPropsTest.cpp` | `isthmus_revision_profile_enables_warm_access` | ✅ | `warm_access=true` | OP-09 |
| `RefundIsthmusTest.cpp` | `RefundIsthmus_refundsLimitMinusUsedCost` | ✅ | operator refund 算术 | 直接单元测 |
| `RollupCostTest.cpp` | `FlzCompressLen_matchesOpGethVectors` | ✅ | op-geth FLZ | |
| `RollupCostTest.cpp` | `NewRollupCostData_countsBytesAndFastLz` | ✅ | empty tx rollup | |
| `TestOpStackTransactionExecutorFixture.cpp` | `l1_fee_recipient_gets_fee_on_success` | ✅ | TE E2E L1 literal vs formula | FIX-05 交叉 |
| `TestOpStackTransactionExecutorFixture.cpp` | `signed_rlp_rollup_matches_l1_cost_formula` | ✅ | signed RLP rollup builder | FIX-05 |
| `TestOpStackTransactionExecutorFixture.cpp` | `FIX05_signed_rlp_rollup_execute_e2e` | ✅ | signed RLP → receipt.l1Fee + recipient | FIX-05 / ADR-012 |
| `TestOpStackTransactionExecutorFixture.cpp` | `operator_fee_recipient_gets_fee_on_success` | ✅ | TE operator E2E + scalar/constant sidecar | FIX-01 / OP-01 |
| `TestOpStackTransactionExecutorFixture.cpp` | `insufficient_balance_fails_before_execution` | 🟡 | TE balance fail | |
| `TestOpStackTransactionExecutorFixture.cpp` | `revert_keeps_l1_fee_and_operator_fee` | ✅ | TE revert L1 + operator literal + recipient | FIX-07 |
| `TestOpStackTransactionExecutorFixture.cpp` | `l1_attributes_deposit_via_te` | ✅ | TE E2E depositNonce + depositor nonce | FIX-06 / ADR-011 |
| `TestOpStackTransactionExecutorFixture.cpp` | `deposit_mint_applied_without_fee_routing` | 🟡 | TE deposit mint | 无 nonce/gasUsed |
| `TestOpStackTransactionExecutorFixture.cpp` | `deposit_skips_fee_routing_recipients` | ✅ | deposit 无 fee | |
| `TestOpStackTransactionExecutorFixture.cpp` | `deposit_failure_reverts_but_keeps_mint` | ✅ | TE deposit fail + gasUsed=21000 | mint/nonce ✅ |
| `TestOpStackTransactionExecutorFixture.cpp` | `hard_failure_status_propagates_without_state_commit` | ✅ | TE INVALID 不 commit + L1/operator literal | 文档化 FB 行为 |
| `TestOpStackTransactionExecutorFixture.cpp` | `executor_input_build_applies_warm_destination_for_call` | ✅ | OP-09 E2E | |
| `TestOpStackTransactionExecutorFixture.cpp` | `executor_input_build_clears_warm_destination_for_create` | ✅ | OP-09 E2E | |

---

## 按域汇总

| 域 | ✅ | 🟡 | 🔴 | 小计 |
|----|----|----|-----|------|
| Fee / L1 / operator 单元 | 11 | 0 | 0 | 11 |
| Settlement / refund 编排 | 6 | 0 | 0 | 6 |
| Floor gas (7623) | 9 | 0 | 0 | 9 |
| Rollup / FLZ | 2 | 0 | 0 | 2 |
| Deposit | 12 | 1 | 0 | 13 |
| L1Block / attributes | 11 | 0 | 0 | 11 |
| Kernel inherited (6780/2537/7702) | 6 | 0 | 0 | 6 |
| Precheck / balance / blob | 16 | 1 | 0 | 17 |
| Smoke / E2E (`ExecuteViaHost` + TE) | 8 | 6 | 0 | 14 |
| Misc (CalcRefund, evmone, props, hook) | 7 | 0 | 0 | 7 |
| **合计** | **88** | **8** | **0** | **96** |

---

## 汇总

| 指标 | 初审计 (65) | 复审计 @ `54e17a62c` (86) | Wave 2 (96) |
|------|-------------|---------------------------|-------------|
| 测试文件数 | 29 | **29** | **29** |
| 用例总数 | 65 | **86** | **96** |
| ✅ 断言 | 49 | **74** | **88** |
| 🟡 断言 | 15 | **12** | **8** |
| 🔴 断言 | 1 | **0** | **0** |

### Stale 修正摘要

| 初审计项 | 复审计 @ `54e17a62c` |
|----------|----------------------|
| `DepositNoFeeRoutingTest` `gasUsed=gasLimit` 🔴 | ✅ actual gas 21000 + entry-failure 用例 |
| 8 文件手动 `m_isIsthmus` | **3 处**有意单元测；生产/ smoke 自动 Isthmus |
| TE 缺 operator fee E2E | ✅ `operator_fee_recipient_gets_fee_on_success` |
| `L1AttributesDepositTest` L1 `>0` only | ✅ literal vs formula + receipt meta |

### Wave 2 闭合摘要（FIX-04～07/09～11）

| FIX | 审计引用 | 断言变更 |
|-----|----------|----------|
| FIX-04 | D2-1 | `FIX04_FjordL1CostSolidityParity_matchesOpGeth` ✅ |
| FIX-05 | D2-2 | `FIX05_signed_rlp_rollup_execute_e2e` + TE L1 literal ✅ |
| FIX-06 | D5-4 | `l1_attributes_deposit_via_te` ✅ |
| FIX-07 | D1-1 | `revert_keeps_l1_fee_and_operator_fee` ✅ |
| FIX-09 | D7-1 | BlobGasBalance + DepositTxPreCheck 4844 形状 6 用例 ✅ |
| FIX-11 | ADR-013 | `opStackExecuteViaHost_refunds_existence_cost_when_authority_already_exists` ✅ |

### 结论

- **🔴 0 例** — OP-04 修正 deposit REVERT gas 断言；Wave 2 无新增 🔴。
- **🟡 8 例** — 主要为 `OpStackExecuteViaHostSmokeTest` operator `>0`（4）、`CanTransferTest`/`EmptyCodeHookTest` 浅覆盖（2）、TE `insufficient_balance`/`deposit_mint` 弱断言（2）。
- **Task 10 状态：** **DONE**（Wave 2 sign-off FIX-14）

---

## 合入主报告

本文件 Part 3 表可直接粘贴至 `docs/audits/2026-06-20-opstack-isthmus-audit.md` §Part 3 — 测试断言审计。
