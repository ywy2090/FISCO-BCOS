# Task 10 — OPStack Isthmus 测试用例清单

**范围：** `bcos-evm/test/opstack/*.cpp`、`transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`  
**初审计日期：** 2026-06-20  
**复审计 commit：** `54e17a62c`（2026-06-21）  
**方法：** `grep -rn "BOOST_AUTO_TEST_CASE" bcos-evm/test/opstack/ transaction-executor/tests/TestOpStackTransactionExecutorFixture.cpp`

---

## 汇总

| 指标 | 初审计 | @ `54e17a62c` |
|------|--------|---------------|
| 测试文件数 | 29 | **29**（opstack 28 + executor fixture 1） |
| `BOOST_AUTO_TEST_CASE` 总数 | 65 | **86** (+21 remediation) |
| 断言审计 | 49✅ / 15🟡 / 1🔴 | **74✅ / 12🟡 / 0🔴** — 见 `task10-assertions.md` |

| 文件 | 用例数 | 关联能力 / 场景 |
|------|--------|-----------------|
| `BlobGasBalanceTest.cpp` | 3 | EIP-4844 blob fee cap + buyGas 扣款 |
| `CalcRefundTest.cpp` | 3 | `postExecuteGasSettlement` / EIP-7623 floor |
| `CanTransferTest.cpp` | 2 | deposit value transfer / predeploy 转账 |
| `DepositMintTest.cpp` | 1 | deposit mint + nonce + depositNonce |
| `DepositNoFeeRoutingTest.cpp` | 3 | deposit 无 fee；REVERT actual gas；entry OOG |
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
| `L1AttributesDepositTest.cpp` | 1 | L1 attributes → user tx L1/operator literal |
| `L1BlockGetterTest.cpp` | 3 | L1Block getter / baseFee / number |
| `L1BlockPredeployTest.cpp` | 5 | Isthmus fixture setter/getter / ACL / IL1Block |
| `OpStack67802537KernelSmokeTest.cpp` | 2 | **OP-08** 2537 MSM + 6780 same-tx selfdestruct |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | 3 | 7702 auth 传播 + intrinsic 25000×n |
| `OpStackExecuteViaHostSmokeTest.cpp` | 4 | fee 路由 / balance / revert / hard fail |
| `OpStackFeeTest.cpp` | 8 | Fjord L1 + Isthmus operator + min-bound |
| `OpStackFloorGasTest.cpp` | 9 | EIP-7623 `floorDataGas` + entry check |
| `OpStackSettlementTest.cpp` | 2 | buyGas/refundGas 全路由 + hard fail |
| `OpStackTxInputBuilderTest.cpp` | 4 | deposit / 7702 / signed RLP rollup |
| `OpStackTxPropsTest.cpp` | 3 | tx-entry warm + Isthmus warm_access |
| `RefundIsthmusTest.cpp` | 1 | Isthmus operator refund |
| `RollupCostTest.cpp` | 2 | FLZ / rollup cost data vs op-geth |
| `TestOpStackTransactionExecutorFixture.cpp` | 10 | TE E2E（fee / deposit / warm / operator） |
| **合计** | **86** | |

---

## 新增用例（65 → 86，remediation）

| 文件 | 新增用例 | OP ID |
|------|----------|-------|
| `BlobGasBalanceTest.cpp` | `buy_gas_deducts_*`, `buy_gas_rejects_*` | OP-06 |
| `DepositNoFeeRoutingTest.cpp` | `deposit_entry_failure_*` | OP-05 |
| `L1BlockGetterTest.cpp` | `basefee_getter`, `number_getter` | OP-14 |
| `L1BlockPredeployTest.cpp` | `pure_getters_*`, `isFeatureEnabled_*` | OP-14 |
| `OpStack67802537KernelSmokeTest.cpp` | 2 cases（新文件） | OP-08 |
| `OpStack7702ExecuteViaHostPropagationTest.cpp` | intrinsic below/charges 25000 | OP-07 |
| `OpStackFeeTest.cpp` | Fjord min-bound ×3 | OP-11 |
| `OpStackTxInputBuilderTest.cpp` | signed RLP rollup ×2 | OP-02 |
| `OpStackTxPropsTest.cpp` | create warm clear + warm_access profile | OP-09 |
| `TestOpStackTransactionExecutorFixture.cpp` | `operator_fee_*`, warm destination ×2 | OP-01, OP-09 |

---

## 共享模式 / 审计锚点（@ `54e17a62c`）

| 模式 | 出现 | 说明 |
|------|------|------|
| **手动 `m_isIsthmus = true`** | `OpStackSettlementTest` (×2), `RefundIsthmusTest` (×1) | **3 处** — 直接测 `OpStackTxExecutor` 单元；非 smoke 路径 |
| **自动 Isthmus profile** | `OpStackExecuteViaHost` + TE executor | `isIsthmusOrchestrationProfile` + `makeIsthmusRevisionConfig` |
| **deposit REVERT actual gas** | `DepositNoFeeRoutingTest::deposit_failure_*` | ✅ `gasUsed=21000`（原 🔴 gasLimit 已删除） |
| **deposit entry gasLimit** | `DepositNoFeeRoutingTest::deposit_entry_failure_*` | ✅ entry OOG → gasLimit |
| **operator fee E2E** | `TestOpStackTransactionExecutorFixture::operator_fee_recipient_*` | ✅ receipt + recipient balance |
| **operator fee 弱断言** | `OpStackExecuteViaHostSmokeTest::l1_fee_recipient_*` | L1=50 literal ✅；operator 仍 `>0` 🟡 |

---

## 范围外（交叉引用）

| 文件 | 说明 |
|------|------|
| `task8-inherited-smoke.md` | ETH reference inherited rows + OP-08/09 |
| `task3-operator-fee.md` | operator fee 公式 / wiring 深审 |
| `task4-deposit.md` | deposit 失败 gas 语义 |
