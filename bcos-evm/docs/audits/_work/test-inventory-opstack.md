# Task 10 — OPStack Isthmus 测试用例清单

**范围：** `bcos-evm/test/opstack/*.cpp`、`transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`  
**初审计日期：** 2026-06-20  
**复审计 commit：** `54e17a62c`（2026-06-21）  
**Wave 2 sign-off：** 2026-06-21  
**方法：** `grep -rn "BOOST_AUTO_TEST_CASE" bcos-evm/test/opstack/ transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`

---

## 汇总

| 指标 | 初审计 | @ `54e17a62c` | Wave 2 |
|------|--------|---------------|--------|
| 测试文件数 | 29 | **29** | **29**（opstack 28 + executor fixture 1） |
| `BOOST_AUTO_TEST_CASE` 总数 | 65 | **87** | **96** (+9 Wave 2) |
| 断言审计 | 49✅ / 15🟡 / 1🔴 | **74✅ / 12🟡 / 0🔴** | **88✅ / 8🟡 / 0🔴** — 见 `task10-assertions.md` |

| 文件 | 用例数 | 关联能力 / 场景 |
|------|--------|-----------------|
| `BlobGasBalanceTest.cpp` | 6 | EIP-4844 preCheck 形状 + buyGas + executeViaHost blob 扣款（FIX-09） |
| `CalcRefundTest.cpp` | 3 | `postExecuteGasSettlement` / EIP-7623 floor |
| `CanTransferTest.cpp` | 2 | deposit value transfer / predeploy 转账 |
| `DepositMintTest.cpp` | 1 | deposit mint + nonce + depositNonce |
| `DepositNoFeeRoutingTest.cpp` | 3 | deposit 无 fee；REVERT actual gas；entry OOG |
| `DepositTxPreCheckTest.cpp` | 7 | deposit precheck / 非 deposit 对照 + eip4844 gate（FIX-09） |
| `Eip7702ApplyAuthorizationTest.cpp` | 1 | EIP-7702 auth 安装 delegation |
| `Eip7702ClearDelegationTest.cpp` | 1 | EIP-7702 清零 delegation |
| `Eip7702DelegationSenderTest.cpp` | 2 | 7702 sender code 形态 precheck |
| `Eip7702PreCheckTest.cpp` | 2 | 7702 auth list on CREATE 拒绝 |
| `EmptyCodeHookTest.cpp` | 1 | L1Block 空 code hook |
| `EvmoneRefundSpikeTest.cpp` | 1 | evmone SSTORE clear refund 语义 |
| `GasFeeCapBalanceTest.cpp` | 1 | gasFeeCap balance 预检 |
| `IsthmusPostExecutionPolicyTest.cpp` | 1 | `makeIsthmusRevisionConfig()` profile |
| `L1AttributesDepositFailureTest.cpp` | 1 | L1 attributes deposit REVERT 不提交 slot |
| `L1AttributesDepositTest.cpp` | 1 | L1 attributes → user tx L1/operator literal + depositNonce |
| `L1BlockGetterTest.cpp` | 3 | L1Block getter / baseFee / number |
| `L1BlockPredeployTest.cpp` | 5 | Isthmus fixture setter/getter / ACL / IL1Block |
| `OpStack67802537KernelSmokeTest.cpp` | 2 | **OP-08** 2537 MSM + 6780 same-tx selfdestruct |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | 4 | 7702 auth 传播 + intrinsic 25000×n + existence refund（FIX-11） |
| `OpStackExecuteViaHostSmokeTest.cpp` | 4 | fee 路由 / balance / revert / hard fail |
| `OpStackFeeTest.cpp` | 9 | Fjord L1 + Isthmus operator + min-bound + Solidity parity（FIX-04） |
| `OpStackFloorGasTest.cpp` | 9 | EIP-7623 `floorDataGas` + entry check |
| `OpStackSettlementTest.cpp` | 2 | buyGas/refundGas 全路由 + hard fail |
| `OpStackTxInputBuilderTest.cpp` | 5 | deposit / 7702 / signed RLP rollup / 4844 decode（FIX-09） |
| `OpStackTxPropsTest.cpp` | 3 | tx-entry warm + Isthmus warm_access |
| `RefundIsthmusTest.cpp` | 1 | Isthmus operator refund |
| `RollupCostTest.cpp` | 2 | FLZ / rollup cost data vs op-geth |
| `TestOpStackTransactionExecutorFixture.cpp` | 13 | TE E2E（fee / deposit / warm / operator / FIX-05/06/07） |
| **合计** | **96** | |

---

## Wave 2 新增用例（87 → 96）

| 文件 | 新增用例 | FIX |
|------|----------|-----|
| `BlobGasBalanceTest.cpp` | `blob_hashes_without_blob_gas_fee_cap_is_rejected`, `blob_hashes_rejected_when_eip4844_disabled`, `opStackExecuteViaHost_deducts_blob_fee_on_success` | FIX-09 |
| `DepositTxPreCheckTest.cpp` | `non_deposit_rejects_blob_fields_when_eip4844_disabled` | FIX-09 |
| `OpStackFeeTest.cpp` | `FIX04_FjordL1CostSolidityParity_matchesOpGeth` | FIX-04 |
| `OpStackTxInputBuilderTest.cpp` | `decodes_eip4844_blob_fields_from_extra_bytes` | FIX-09 |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | existence refund（FIX-11；与 OP-07 intrinsic 同文件） | FIX-11 |
| `TestOpStackTransactionExecutorFixture.cpp` | `signed_rlp_rollup_matches_l1_cost_formula`, `FIX05_signed_rlp_rollup_execute_e2e`, `l1_attributes_deposit_via_te`；`revert_keeps_l1_fee` 重命名为 `revert_keeps_l1_fee_and_operator_fee` | FIX-05/06/07 |

---

## 共享模式 / 审计锚点（Wave 2 @ 2026-06-21）

| 模式 | 出现 | 说明 |
|------|------|------|
| **手动 `m_isIsthmus = true`** | `OpStackSettlementTest` (×2), `RefundIsthmusTest` (×1) | **3 处** — 直接测 `OpStackTxExecutor` 单元；非 smoke 路径 |
| **自动 Isthmus profile** | `OpStackExecuteViaHost` + TE executor | `isIsthmusOrchestrationProfile` + `makeIsthmusRevisionConfig` |
| **deposit REVERT actual gas** | `DepositNoFeeRoutingTest::deposit_failure_*` | ✅ `gasUsed=21000` |
| **operator fee TE E2E literal** | `TestOpStackTransactionExecutorFixture::revert_keeps_l1_fee_and_operator_fee` | ✅ FIX-07 |
| **signed RLP TE E2E** | `FIX05_signed_rlp_rollup_execute_e2e` | ✅ FIX-05 / ADR-012 |
| **L1 attributes TE E2E** | `l1_attributes_deposit_via_te` | ✅ FIX-06 / ADR-011 |
| **operator fee 弱断言** | `OpStackExecuteViaHostSmokeTest` (×4) | L1=50 literal ✅；operator 仍 `>0` 🟡（非 Wave 2 阻断） |

---

## 范围外（交叉引用）

| 文件 | 说明 |
|------|------|
| `stEIP7702_delegation.json` | 📋 **hand-crafted smoke** — 非 `GeneralStateTests/stEIP7702` inherited 7702 compliance；仅 ExecuteViaEth/OpStack fixture pipeline smoke（FIX-10 / ADR-013） |
| `task8-inherited-smoke.md` | ETH reference inherited rows + OP-08/09 + FIX-12 profile footnote |
| `task3-operator-fee.md` | operator fee 公式 / wiring 深审 |
| `task4-deposit.md` | deposit 失败 gas 语义 |
