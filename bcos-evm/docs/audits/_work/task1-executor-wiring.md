# Task 1: Isthmus Profile + Executor Integration Wiring

**Commit:** `54e17a62c` (`feat(opstack): align L1Block IL1Block surface and close Isthmus remediation`)  
**Prior audit:** `f989f073f` — 🔴 `m_isIsthmus` 未接线  
**Remediation verified:** OP-01 (`m_isIsthmus` 生产接线), OP-09/OP-09b (`warm_access` + `applyDefaultTxProps`)

## Part 1 rows

| 能力 | 层级 | 清单来源 | Matrix 状态 | 深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|------|------|-----------|---------|--------------|---------|------|
| makeIsthmusRevisionConfig | revision profile | matrix inherited | inherited | smoke | ✅ | Isthmus PRAGUE EVM | `RevisionConfig.h:62-72` | op-geth Isthmus EVM rules | `RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields`; `OpStackTxPropsTest::isthmus_revision_profile_enables_warm_access` | — |
| Isthmus executor integration wiring (S3) | executor-integration | supplement | explicit | 深审 | ✅ | Isthmus operator fee MUST | `OpStackTransactionExecutorImpl.h:197-211`; `OpStackExecuteViaHost.cpp:79-82,249` | op-geth charges operator fee when Isthmus active | `TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success`; `OpStackExecuteViaHostSmokeTest::l1_fee_recipient_gets_fee_on_success` | 🟡 literal / revert 路径弱断言 |
| EIP-2929 tx-entry destination warm | tx input | matrix inherited | inherited | smoke | ✅ | EIP-2929 tx warm | `OpStackTransactionExecutorImpl.h:206` `applyDefaultTxProps` | Berlin+ warm destination | `OpStackTxPropsTest::applyDefaultTxProps_sets_warm_destination_from_kind`; `TestOpStackTransactionExecutorFixture::executor_input_build_applies_warm_destination_for_call` | — |
| RevisionConfig `warm_access` (Isthmus) | revision profile | matrix feature-gated (profile-only) | feature-gated | smoke | ✅ | ADR-004 profile-only; EthChainPolicy Berlin+ | `makeIsthmusRevisionConfig` `warm_access=true` (`RevisionConfig.h:66`) | runtime 2929 via revision + flag | `RevisionConfigProfileTest::isthmus_helper_sparse_profile_all_fields` | profile-only 语义仍适用 |

## Part 2 — 偏差与接线核对

### ✅ S3: `m_isIsthmus` 生产路径已接线（OP-01 闭合）

**初审计（f989f073f）：** `opStackExecuteViaHostTx()` 未设 `m_isIsthmus` → operator fee 端到端失效。

**54e17a62c 验证：**

1. **Executor 层显式设置** — `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()`（`:210-211`）：
   ```cpp
   input.opTxExecutor.m_isIsthmus = true;  // Isthmus executor always activates operator fee
   ```

2. **编排层二次保障** — `opStackExecuteViaHost()`（`:79-82`）在 `isIsthmusOrchestrationProfile(revisionConfig)` 时再次设 `m_isIsthmus = true`（与 `makeIsthmusRevisionConfig()` 稀疏 profile 一致）。

3. **消费路径** — `OpStackTxExecutor::buyGas` / `refundIsthmusOperatorCost` 门控 `m_isIsthmus && m_operatorCostFunc`（`OpStackTxExecutor.cpp`）；receipt meta 写入门控 `OpStackExecuteViaHost.cpp:249-258`；`makeReceipt` 调用 `setOperatorFee`（`OpStackTransactionExecutorImpl.h:261-264`）。

**对照 op-geth：** Isthmus 激活时 operator fee 计入 balance check 与 settlement — 生产路径现已可达 ✅。

**测试路径：** OP-15 已清理 opstack 直连 `opStackExecuteViaHost` 测试中冗余 `m_isIsthmus=true`；`isIsthmusOrchestrationProfile` 自动启用。`RefundIsthmusTest` 保留直连 `OpStackTxExecutor` 单元测的手动 flag（合理）。

### ✅ OP-09: `applyDefaultTxProps` + `warm_access`（OP-09b）

| 项 | 54e17a62c | 初审计 |
|----|-----------|--------|
| `applyDefaultTxProps` 生产调用 | ✅ `opStackExecuteViaHostTx` `:206` | ❌ 未调用 |
| `warm_access` on Isthmus profile | ✅ `makeIsthmusRevisionConfig` `:66` | 🟡 未设 |
| tx-entry warm destination | ✅ `applyDefaultTxProps` → `setWarmDestinationFromKind` | 仅测试 helper |

### Executor 输入 checklist（§3.6）

| 字段 / 步骤 | 生产接线 | 证据 |
|-------------|----------|------|
| `revisionConfig` | ✅ | `makeIsthmusRevisionConfig()` `:197` |
| `blockInfo` (baseFee, blobBaseFee) | ✅ | `resolveOpStackBaseFee` / `resolveOpStackBlobBaseFee` + `buildOpStackBlockInfo` `:198-201` |
| `blockHashes` | ✅ | `buildFiscoBlockHashes` `:202-203` |
| `fillGasCaps` | ✅ | `:204` |
| `fillWeb3Fields` | ✅ | `:205` |
| `applyDefaultTxProps` | ✅ | `:206` |
| `rollupCostData` | ✅ | `buildRollupCostData` `:207` |
| `gasPoolSubGasHook` | ✅ | `:208-209` |
| `m_isIsthmus` | ✅ | `:210-211` |
| receipt `setL1Fee` | ✅ | `makeReceipt` `:257-259` |
| receipt `setOperatorFee` | ✅ | `makeReceipt` `:261-264` |
| receipt `setDepositNonce` | ✅ | `makeReceipt` `:266-269` |

## Part 3 — executor / 编排测试证据

| 文件 | 用例 | 断言状态 | 备注 |
|------|------|----------|------|
| `TestOpStackTransactionExecutorFixture.cpp` | `operator_fee_recipient_gets_fee_on_success` | ✅ | TE E2E：`receipt->operatorFee() != 0x0`；`OP_OPERATOR_FEE_RECIPIENT` balance `>0` |
| `TestOpStackTransactionExecutorFixture.cpp` | `l1_fee_recipient_gets_fee_on_success` | ✅ | L1 fee TE E2E |
| `TestOpStackTransactionExecutorFixture.cpp` | `executor_input_build_applies_warm_destination_for_call` | ✅ | `applyDefaultTxProps` + `warmDestination` |
| `TestOpStackTransactionExecutorFixture.cpp` | `executor_input_build_clears_warm_destination_for_create` | ✅ | CREATE 清除 warm |
| `TestOpStackTransactionExecutorFixture.cpp` | `revert_keeps_l1_fee` | 🟡 | L1 fee ✅；未断言 operator fee |
| `TestOpStackTransactionExecutorFixture.cpp` | `hard_failure_status_propagates_without_state_commit` | 🟡 | L1 meta ✅；无 operator fee 断言 |
| `OpStackExecuteViaHostSmokeTest.cpp` | `l1_fee_recipient_gets_fee_on_success` | ✅ | 编排 smoke：`receiptMeta.operatorFee > 0`；无手动 `m_isIsthmus` |
| `OpStackTxPropsTest.cpp` | `applyDefaultTxProps_sets_warm_destination_from_kind` | ✅ | helper 契约 |
| `OpStackTxPropsTest.cpp` | `isthmus_revision_profile_enables_warm_access` | ✅ | profile `warm_access` |
| `RefundIsthmusTest.cpp` | `RefundIsthmus_refundsLimitMinusUsedCost` | ✅ | 单元：`refundIsthmusOperatorCost` 公式；手动 `m_isIsthmus`（直连 executor，非 wiring 缺口） |

### 🟡 剩余弱覆盖（非接线阻断）

1. **Literal 断言** — `operator_fee_recipient_gets_fee_on_success` 用 `!= 0x0` / `balance > 0`，未对照 op-geth 精确 scalar×gas+constant。
2. **Revert / hard-failure** — TE fixture 未断言 operator fee 保留或 recipient 余额。
3. **Receipt scalar/constant** — `OpStackExecuteViaHost` 在 fee params 非零时写入 `operatorFeeScalar`/`operatorFeeConstant`（`:254-257`）；TE fixture 未断言 meta 标量字段（OP-13 在编排层有覆盖，TE 层仍 🟡）。

## makeIsthmusRevisionConfig 核对（vs capability-matrix OPStack 列）

| 字段 | 值 | Matrix 期望 | 状态 |
|------|-----|-------------|------|
| `revision` | `EVMC_PRAGUE` | inherited (6780/1153/5656 via revision) | ✅ |
| `warm_access` | `true` | feature-gated (profile-only; 现显式赋值) | ✅ |
| `eip7623` | `true` | explicit orchestration | ✅ |
| `eip7702` | `true` | inherited | ✅ |
| `eip4844` | `true` | inherited | ✅ |
| `prague_post_execution` | `false` | unsupported | ✅ |
| `calldata_floor_per_token` | `10` | EIP-7623 floor | ✅ |
| `eip2537` / `eip7212` / `eip7823` | `false` (default) | inherited via revision / unsupported 7212 on PRAGUE | ✅ 稀疏 profile |
| `eip1153` / `eip6780` / `eip5656` | `false` (default) | inherited via `revision` (ADR-004) | ✅ profile-only |

## 判定摘要

| 审计项 | 初审计 (f989f073f) | 54e17a62c |
|--------|-------------------|-----------|
| makeIsthmusRevisionConfig vs matrix | PASS ✅ | PASS ✅（+ `warm_access`) |
| `m_isIsthmus` 生产路径 | FAIL 🔴 | PASS ✅ |
| `applyDefaultTxProps` 生产接线 | FAIL 🔴 | PASS ✅ |
| `warm_access` Isthmus profile | FAIL 🟡 | PASS ✅ |
| Executor input checklist | PARTIAL | PASS ✅ |
| TE operator fee E2E | FAIL 🟡 | PASS ✅（弱 literal） |

**Task 1 状态：** DONE — P0 接线项已独立验证闭合；剩余 🟡 为测试强度，不构成 S3 wiring 阻断。

---

## Wave 3 复审计附录（@ `52dda0921`，严格 op-geth `e8800cffe`）

| # | 项 | op-geth | FB | Wave 3 |
|---|-----|---------|-----|--------|
| W3-1 | 生产接入 | miner/worker | `libinitializer/Initializer.cpp:339-342` `ExecutionPath::OpStack` | ✅ 确认非仅测试 |
| W3-2 | baseFee 来源 | `evm.Context.BaseFee` | OPF1 extraData | ✅ **CLOSED R3-ORCH-1** |
| W3-3 | blobBaseFee 来源 | `evm.Context.BlobBaseFee` | calcOpStackBlobBaseFee(0)=1 | ✅ **CLOSED R3-ORCH-2** |
| W3-4 | L1 cost fork | `NewL1CostFunc` 多分支 | `wireL1CostFuncWithState` + ADR-014 | ✅ **CLOSED R3-ORCH-3** |
| W3-5 | Wave 2 OP-01/09 | — | 再验证 ✅ | 无回归 |

**Wave 3 Task 1 判定：** ✅ wiring PASS；✅ R3-ORCH-1/2/3 CLOSED；🟡 sealer OPF1 生产写路径 follow-up。
