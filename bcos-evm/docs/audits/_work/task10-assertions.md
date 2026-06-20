# Task 10 — OPStack Isthmus 测试断言审计

**日期：** 2026-06-20  
**Skill：** `fisco-evm-test-coverage/references/assertion-audit.md`  
**清单：** `_work/test-inventory-opstack.md`  
**金标准：** op-geth v1.101702.2 @ `e8800cffe`（`rollup_cost.go`、`state_transition.go`、`deposit_tx.go`）；optimism-specs `689a96f` Isthmus/deposit 章节  
**交叉引用：** Task 1 wiring、Task 3 operator fee、Task 4 deposit、Task 8 ETH reference 断言等级

---

## 断言等级说明

| 符号 | 含义 |
|------|------|
| ✅ | 期望值与 op-geth / optimism specs / FB 单元向量一致；路径有效 |
| 🟡 | 方向对但偏弱：smoke、`>0` 代替 literal、缺 post-state/gas/nonce、手动 `m_isIsthmus`、生产 wiring 未覆盖 |
| 🔴 | 期望值编码实现偏离（断言 gasLimit 而 op-geth 要 actual gas）；或断言与金标准直接冲突 |

---

## 重点发现（Task 10 高亮）

### 1. 手动 `m_isIsthmus = true`（8 文件，生产未接线）

| 文件 | 行 | 用例 |
|------|-----|------|
| `CanTransferTest.cpp` | `:51` | 两例 |
| `DepositMintTest.cpp` | `:68` | `deposit_mint_*` |
| `DepositNoFeeRoutingTest.cpp` | `:79` | 两例 |
| `L1AttributesDepositTest.cpp` | `:95` | user tx 段 |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | `:116` | 7702 传播 |
| `OpStackExecuteViaHostSmokeTest.cpp` | `:109` | 四例 smoke |
| `OpStackSettlementTest.cpp` | `:33`, `:75` | settlement 两例 |
| `RefundIsthmusTest.cpp` | `:30` | operator refund |

**影响：** operator fee buy/refund/route/receipt 在 `OpStackTransactionExecutorImpl` E2E 路径默认 **不启用**（Task 1 S3 / Task 3 D3-1）。单元测试正确但 **不证明生产 Isthmus 合规**。

### 2. `DepositNoFeeRoutingTest` — gasLimit 断言 🔴

`deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce`（`:128`）：

```cpp
BOOST_CHECK_EQUAL(output.gasUsed, 50'000);  // == message.gas / depositTx.gas
```

- **FB 行为：** 所有非 SUCCESS deposit 设 `gasUsed = gasLimit`（`OpStackExecuteViaHost.cpp:176–177`）。
- **op-geth Isthmus/Regolith+：** EVM `REVERT`（`PUSH1 0 PUSH1 0 REVERT`）→ **实际 metered gas**（`state_transition.go:681–688`）。
- **判定：** 🔴 — 测试将偏离编码为期望；mint/nonce 断言 ✅，gas 断言与金标准冲突（Task 4 D4-2/D4-4）。

### 3. 缺失 E2E operator fee（`TestOpStackTransactionExecutorFixture.cpp`）

7 个 executor E2E 用例 **均无**：

- `receipt->operatorFee()` 或等价 metadata 断言
- `OP_OPERATOR_FEE_RECIPIENT` 余额 literal / `>0`
- Isthmus worst-case buyGas balance check（含 operator limit）

`l1_fee_recipient_gets_fee_on_success` 仅断言 `l1Fee != 0x0` 与 L1 recipient `>0`。对比 `OpStackExecuteViaHostSmokeTest` 在同场景下对 operator 有 `BOOST_CHECK_GT` + `receiptMeta.operatorFee`（仍 🟡，因手动 flag + 无 literal）。

**缺口：** 修复 Task 1 wiring 后需补 **生产路径** operator fee E2E（buy → execute → refund → vault → receipt）。

---

## Part 3 — 全表断言审计（65 用例）

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `BlobGasBalanceTest.cpp` | `blob_gas_fee_cap_under_blob_base_fee_is_rejected` | ✅ | EIP-4844 blob gas price check | `InsufficientFunds` |
| `CalcRefundTest.cpp` | `Settlement_capBinds` | ✅ | EIP-3529 cap + settlement 算术 | 字面量 gasUsed/refund |
| `CalcRefundTest.cpp` | `Settlement_floorDataGasBumpsGasUsed` | ✅ | EIP-7623 floor | gasUsed=700 @ floor |
| `CalcRefundTest.cpp` | `EvmoneParity_noDoubleCount` | ✅ | evmone gas_left 语义 | 防 double-count |
| `CanTransferTest.cpp` | `value_transfer_rejected_when_sender_balance_insufficient` | ✅ | deposit value 转账 | `InsufficientFunds`；**手动 `m_isIsthmus`** |
| `CanTransferTest.cpp` | `transfer_to_predeploy_allowed_if_funded` | 🟡 | predeploy 可接收 value | 仅否定性 status；无 SUCCESS/balance |
| `DepositMintTest.cpp` | `deposit_mint_is_applied_before_execution` | 🟡 | `deposits.md` mint 保留 | balance=100 ✅；无 nonce/gasUsed/depositNonce；**手动 flag** |
| `DepositNoFeeRoutingTest.cpp` | `deposit_skips_fee_routing_recipients` | ✅ | deposit 无 fee 路由 | 四 recipient=0；**手动 flag** |
| `DepositNoFeeRoutingTest.cpp` | **`deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce`** | **🔴** | Regolith+ REVERT → **actual gas** | mint/nonce ✅；**`gasUsed=gasLimit` 与 op-geth 🔴** |
| `DepositTxPreCheckTest.cpp` | `system_deposit_is_rejected` | ✅ | `preCheck():354–356` | system → Malformed |
| `DepositTxPreCheckTest.cpp` | `deposit_skips_nonce_and_fee_checks_but_still_subtracts_gas_pool` | ✅ | deposit 跳过 nonce/fee | gas pool hook |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_nonce_mismatch` | ✅ | nonce check | 对照 |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_invalid_eip1559_caps` | ✅ | tip≤feeCap | 对照 |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_blob_fee_cap_under_base_fee` | ✅ | blob cap | 对照 |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_auth_list_on_create` | ✅ | 7702 on CREATE | 对照 |
| `Eip7702ApplyAuthorizationTest.cpp` | `valid_auth_installs_delegation_invalid_is_ignored_and_refund_added` | ✅ | EIP-7702 delegation code | `0xEF0100‖addr`；nonce+1 |
| `Eip7702ClearDelegationTest.cpp` | `auth_with_zero_target_clears_existing_delegation_code` | ✅ | EIP-7702 clear | code 空；nonce+1 |
| `Eip7702DelegationSenderTest.cpp` | `sender_with_delegation_code_passes_precheck` | ✅ | 7702 sender 形态 | 无 error |
| `Eip7702DelegationSenderTest.cpp` | `sender_with_non_delegation_code_is_rejected` | ✅ | 非 delegation code | Malformed |
| `Eip7702PreCheckTest.cpp` | `rejects_authorization_list_on_create` | ✅ | CREATE+auth | Malformed |
| `Eip7702PreCheckTest.cpp` | `rejects_explicit_empty_authorization_list` | ✅ | 空 auth list | Malformed |
| `EmptyCodeHookTest.cpp` | `top_level_call_hits_chain_precompile_hook_on_empty_code` | 🟡 | L1Block hook | 仅 REVERT；未断言 hook 命中或 slot |
| `EvmoneRefundSpikeTest.cpp` | `SstoreClear_recordsGasLeftAndRefund` | ✅ | EIP-3529 SSTORE clear refund | refund=4800；DEFERRED_REFUND |
| `GasFeeCapBalanceTest.cpp` | `gas_fee_cap_balance_check_rejects_insufficient_sender` | ✅ | buyGas balance | NotEnoughCash；无 Isthmus operator 叠加 |
| `IsthmusPostExecutionPolicyTest.cpp` | `isthmus_revision_config_disables_prague_post_execution` | ✅ | `makeIsthmusRevisionConfig()` | profile 字面量 |
| `L1AttributesDepositFailureTest.cpp` | `failed_l1_attributes_deposit_does_not_commit_slot_changes` | ✅ | setter REVERT 不 commit | 无 slot diff；未断言 nonce/gasUsed |
| `L1AttributesDepositTest.cpp` | `l1_attributes_deposit_updates_l1block_and_affects_following_user_tx` | 🟡 | L1Block + Fjord L1 fee E2E | L1 recipient `>0` only；**手动 `m_isIsthmus`**；无 operator |
| `L1BlockGetterTest.cpp` | `op_host_extension_dispatches_l1block_getter` | ✅ | L1Block getter | output=123456 |
| `L1BlockPredeployTest.cpp` | `setter_unpacks_isthmus_fixture_into_slots` | ✅ | `isthmus_l1_attributes.bin` | slot 字节 vs fixture |
| `L1BlockPredeployTest.cpp` | `setter_rejects_non_depositor_sender` | ✅ | depositor ACL | REVERT + slot 零 |
| `L1BlockPredeployTest.cpp` | `getters_return_slot_values_after_setter` | ✅ | getter selectors | 5× literal u256 |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | `opStackExecuteViaHost_propagates_authorizations_to_executeMessage` | 🟡 | 7702 via opstack 编排 | delegation ✅；**手动 `m_isIsthmus`**；无 fee 路径 |
| `OpStackExecuteViaHostSmokeTest.cpp` | `l1_fee_recipient_gets_fee_on_success` | 🟡 | buyGas + fee routing | L1=50 ✅；operator/base/coinbase `>0`；**手动 flag** |
| `OpStackExecuteViaHostSmokeTest.cpp` | `insufficient_balance_fails_before_execution` | 🟡 | balance pre-check | 无 operator worst-case 断言 |
| `OpStackExecuteViaHostSmokeTest.cpp` | `revert_refunds_unused_gas_and_keeps_l1_fee` | 🟡 | revert + L1 fee | L1=50 ✅；sender 范围断言弱 |
| `OpStackExecuteViaHostSmokeTest.cpp` | `hard_failure_still_refunds_unused_gas_and_routes_fees` | 🟡 | hard fail settlement | 仅 `>0`；无 literal |
| `OpStackFeeTest.cpp` | `FjordL1_emptyTx_matches3203000` | ✅ | `rollup_cost_test.go` Fjord | 3'203'000 |
| `OpStackFeeTest.cpp` | `IsthmusOperator_gas1618_matchesFixture` | ✅ | `rollup_cost_test.go:35` | 1256417826611659930 |
| `OpStackFeeTest.cpp` | `FjordL1_emptyRollupCostData_returnsZero` | ✅ | 空 rollup | L1=0 |
| `OpStackFeeTest.cpp` | `IsthmusOperator_zeroParams_returnsZero` | ✅ | 零 operator 参数 | operator=0 |
| `OpStackFeeTest.cpp` | `LoadOpStackFeeParams_unpacksSlots` | ✅ | `ExtractOperatorFeeParams` | slot 8 子域 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_emptyCalldata_is21000` | ✅ | EIP-7623 / op-geth floor | 21000 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_zeroBytesOnly` | ✅ | floor token 计数 | 100×10 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_nonZeroBytesOnly` | ✅ | 4× token | 100×40 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_mixedZeroAndNonZero` | ✅ | mixed tokens | 250×10 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_singleZeroByte` | ✅ | 边界 | 21010 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_singleNonZeroByte` | ✅ | 边界 | 21040 |
| `OpStackFloorGasTest.cpp` | `FloorDataGas_overflow_returnsError` | ✅ | uint64 overflow | GasUintOverflow |
| `OpStackFloorGasTest.cpp` | `ExecuteEntryFloorCheck_gasLimitBelowFloor_rejects` | ✅ | entry floor check | BelowFloor |
| `OpStackFloorGasTest.cpp` | `ExecuteEntryFloorCheck_gasLimitAtFloor_accepts` | ✅ | entry floor check | ok @ floor |
| `OpStackSettlementTest.cpp` | `Settlement_routesCoinbaseBaseFeeL1AndOperator` | ✅ | buyGas+refundGas 全路由 | sender=16690 等字面量；**手动 flag** |
| `OpStackSettlementTest.cpp` | `HardFailure_stillRefundsUnusedGas` | ✅ | OOG hard fail 仍 settlement | operator=430 等；**手动 flag** |
| `OpStackTxInputBuilderTest.cpp` | `decodes_deposit_extra_transaction_bytes` | ✅ | deposit RLP | 字段 literal |
| `OpStackTxInputBuilderTest.cpp` | `decodes_eip7702_authorization_from_extra_bytes` | ✅ | type-4 RLP | auth 字段 |
| `OpStackTxPropsTest.cpp` | `applyDefaultTxProps_sets_warm_destination_from_kind` | ✅ | EIP-2929 tx-entry | CALL warm / CREATE not |
| `RefundIsthmusTest.cpp` | `RefundIsthmus_refundsLimitMinusUsedCost` | ✅ | `refundIsthmusOperatorCost` | +1118；**手动 flag** |
| `RollupCostTest.cpp` | `FlzCompressLen_matchesOpGethVectors` | ✅ | op-geth FLZ | 0/21/31/202 |
| `RollupCostTest.cpp` | `NewRollupCostData_countsBytesAndFastLz` | ✅ | empty tx rollup | ones=30, fastLz=31 |
| `TestOpStackTransactionExecutorFixture.cpp` | `l1_fee_recipient_gets_fee_on_success` | 🟡 | TE E2E L1 fee | l1Fee≠0；**无 operator E2E** |
| `TestOpStackTransactionExecutorFixture.cpp` | `insufficient_balance_fails_before_execution` | 🟡 | TE balance fail | 无 operator 叠加断言 |
| `TestOpStackTransactionExecutorFixture.cpp` | `revert_keeps_l1_fee` | 🟡 | TE revert + L1 | 无 operator |
| `TestOpStackTransactionExecutorFixture.cpp` | `deposit_mint_applied_without_fee_routing` | 🟡 | TE deposit mint | 无 nonce/gasUsed |
| `TestOpStackTransactionExecutorFixture.cpp` | `deposit_skips_fee_routing_recipients` | ✅ | deposit 无 fee | 三 recipient=0 |
| `TestOpStackTransactionExecutorFixture.cpp` | `deposit_failure_reverts_but_keeps_mint` | 🟡 | TE deposit fail | mint/nonce ✅；**未断言 gasUsed**（未编码 🔴，但也未验证 actual gas） |
| `TestOpStackTransactionExecutorFixture.cpp` | `hard_failure_status_propagates_without_state_commit` | 🟡 | TE INVALID 不 commit | 文档化 FB 行为；l1Fee on receipt 但 storage 未 commit |

---

## 按域汇总

| 域 | ✅ | 🟡 | 🔴 | 小计 |
|----|----|----|-----|------|
| Fee / L1 / operator 单元 | 7 | 0 | 0 | 7 |
| Settlement / refund 编排 | 3 | 0 | 0 | 3 |
| Floor gas (7623) | 9 | 0 | 0 | 9 |
| Rollup / FLZ | 2 | 0 | 0 | 2 |
| Deposit | 7 | 2 | 1 | 10 |
| L1Block / attributes | 5 | 1 | 0 | 6 |
| Precheck / balance / blob | 8 | 1 | 0 | 9 |
| 7702 (inherited) | 6 | 1 | 0 | 7 |
| Smoke / E2E (`ExecuteViaHost` + TE) | 1 | 10 | 0 | 11 |
| Misc (CalcRefund, evmone, props, hook) | 5 | 1 | 0 | 6 |
| **合计** | **49** | **15** | **1** | **65** |

---

## 汇总

| 指标 | 值 |
|------|-----|
| 测试文件数 | **29** |
| 用例总数 | **65** |
| ✅ 断言 | **49** |
| 🟡 断言 | **15** |
| 🔴 断言 | **1** |

### 结论

- **🔴 1 例：** `DepositNoFeeRoutingTest::deposit_failure_*` 的 `gasUsed=gasLimit` 断言与 op-geth Isthmus/Regolith+ REVERT 语义冲突；测试固化实现偏离。
- **🟡 15 例：** 主要风险：(1) **8 文件手动 `m_isIsthmus`**，生产 wiring 未测；(2) **TE fixture 7 例缺 operator fee E2E**；(3) smoke 用 `>0` 代替 op-geth literal；(4) deposit 成功/失败缺 nonce、depositNonce、actual gas 断言。
- **优先补测（P1）：** Task 1 接线后 TE operator fee E2E（vault + receipt literal）；deposit REVERT actual gas vs op-geth 向量；移除/修正 🔴 gasLimit 断言；成功 deposit nonce+1。

---

## 合入主报告

本文件 Part 3 表可直接粘贴至 `docs/audits/2026-06-20-opstack-isthmus-audit.md` §Part 3 — 测试断言审计。
