# Task 3 — OPStack Operator Fee (Isthmus) 审计笔记

**日期：** 2026-06-21（复审计 @ `54e17a62c`）  
**初审计：** 2026-06-20 @ `f989f073f` — 🔴 `m_isIsthmus` 生产未接线  
**范围：** 增补 S1 `OPStack operator fee (Isthmus)`；`OpStackFee.*`、`OpStackTxExecutor.*`、`opStackExecuteViaHost` fee 路径  
**参考：** op-geth `e8800cffe` — `core/types/rollup_cost.go`（`NewOperatorCostFunc`、`newOperatorCostFuncIsthmus`、`ExtractOperatorFeeParams`）、`core/state_transition.go`（`buyGas`、`refundIsthmusOperatorCost`、fee routing）  
**Specs：** ethereum-optimism-specs `689a96f` — `specs/protocol/isthmus/exec-engine.md` §Operator Fee  
**交叉引用：** Task 1 S3 `m_isIsthmus` 接线（`task1-executor-wiring.md`）

---

## Step 1: Spec + op-geth MUST（operator fee 语义基线）

| 规则 | optimism specs | op-geth |
|------|----------------|---------|
| 公式：`operatorFee = (gas × scalar ÷ 10⁶) + constant` | `exec-engine.md` §Fee Formula | `newOperatorCostFuncIsthmus`（`:253-268`） |
| 预执行 balance check 含 worst-case operator fee（`gas = gas_limit`） | §EVM Fee Semantics #1 | `buyGas():294-307` |
| 买 gas 时扣 worst-case operator fee | §#2 | `buyGas():295-296` |
| 执行后退还 `opFeeWorstCase - opFeeActual` | §#3；actual gas = `gas_limit - gas_used + refunded_gas` | `refundIsthmusOperatorCost():836-845`；`gasUsed()` = `initialGas - gasRemaining` |
| 执行后将 spent operator fee 路由至 OperatorFeeVault | §#4、`predeploys.md` | `state_transition.go:731-732` → `OptimismOperatorFeeRecipient` |
| Deposit tx **不**收 operator fee、无 refund | §Deposit Operator Fees | `!st.msg.IsDepositTx` 分支（`:713+`） |
| 参数来源：L1Block slot 8 或 L1 attributes | §Configuring Operator Fee Parameters | `OperatorFeeParamsSlot`；`ExtractOperatorFeeParams` `[20:24]`/`[24:32]` |
| Isthmus 激活前 operator fee = 0 | §Overview / timestamp activation | `NewOperatorCostFunc` → `!IsOptimismIsthmus` 返回零函数（`:223-226`） |
| Receipt 扩展：scalar/constant 非零时写入 receipt | §Receipts | op-geth receipt 类型扩展 |

**Isthmus 审计锚点：** operator fee 自 Isthmus 起生效；Fjord L1 fee 与之独立叠加（Task 2）。Jovian operator-fee-fix 公式 **不在** Isthmus 审计范围。

---

## Step 2: FB 实现对照

### 公式与参数加载（`OpStackFee.cpp`）

| 项 | FB | op-geth | 一致 |
|----|----|---------|------|
| Isthmus 公式 | `operatorCostIsthmus:58-67` | `newOperatorCostFuncIsthmus` | ✅ |
| 双零短路 | scalar==0 && constant==0 → 0（`:60-63`） | 空 slot → 零函数（`:229-232`） | ✅ |
| Slot 8 读取 | `[20:24]` scalar, `[24:32]` constant（`:91-92`） | `ExtractOperatorFeeParams`（`:656-659`） | ✅ |
| 空 slot 跳过 | `isZeroBytes32` 早退（`:86-89`） | `operatorFeeParams == {}` | ✅ |
| L1Block 写入 | `packOperatorFeeParams`（`L1BlockStorage.cpp:73-88`） | 同 layout | ✅ |
| Fee recipient | `OP_OPERATOR_FEE_RECIPIENT` `0x420…001b` | `OptimismOperatorFeeRecipient` | ✅ |

**单元测试向量（op-geth 同参）：**

```
gas=1618, scalar=1439103868, constant=1256417826609331460
→ 1256417826611659930  // rollup_cost_test.go:35
```

`OpStackFeeTest::IsthmusOperator_gas1618_matchesFixture` ✅

### 交易生命周期（`OpStackTxExecutor.cpp`）

| 阶段 | FB | op-geth | 一致（当 `m_isIsthmus=true`） |
|------|----|---------|-------------------------------|
| buyGas worst-case | `m_operatorCostFunc(gasLimit)` 加入 `mgval`（`:59-65`） | `OperatorCostFunc(GasLimit)`（`:295`） | ✅ |
| balance check (EIP-1559) | `gasLimit*gasFeeCap + l1 + operatorLimit + value`（`:69-73`） | 同模式（`:300-308`） | ✅ |
| deposit 豁免 | `m_isDepositTx` 早退 buyGas/refundGas（`:26,112`） | `IsDepositTx` | ✅ |
| operator refund | `refundIsthmusOperatorCost`：`limit - used`（`:93-108`） | `:836-845` | ✅ |
| fee routing | `m_operatorCostFunc(gasUsed)` → `m_operatorFeeRecipient`（`:137-141`） | `:731-732` | ✅ |
| actual gas 输入 | `postExecuteGasSettlement` → `txData.m_gasUsed`（含 EIP-3529 refund + floor） | `st.gasUsed()` | ✅ 语义对齐 |

### 编排接线（`OpStackExecuteViaHost.cpp` + TE）

| 项 | 行为 | 54e17a62c |
|----|------|-----------|
| `loadOpStackFeeParams` + `m_operatorCostFunc` | 始终绑定（`:87-93`） | ✅ |
| **`m_isIsthmus` 生产接线** | `OpStackTransactionExecutorImpl.h:210-211` 显式 `true`；`opStackExecuteViaHost:79-82` `isIsthmusOrchestrationProfile` 二次保障 | ✅ **OP-01 闭合** |
| buyGas / refund 门控 | `m_isIsthmus && m_operatorCostFunc`（`OpStackTxExecutor.cpp:60,105,147`） | ✅ 生产可达 |
| receipt `operatorFee` | `m_isIsthmus` 时写入 spent fee（`:249-253`） | ✅ |
| receipt `operatorFeeScalar/Constant` | fee params 非零时写入 meta（`:254-258`） | ✅ **OP-13 编排 meta** |
| `makeReceipt` 协议层 | 仅 `setOperatorFee`（`:261-264`）；**未** `setOperatorFeeScalar/Constant` | 🟡 known gap |

---

## Step 3: ✅ Task 1 交叉引用 — `m_isIsthmus` 生产已接线（OP-01）

**初审计（f989f073f）：** `opStackExecuteViaHostTx()` 未设 `m_isIsthmus` → operator fee 端到端失效。

**54e17a62c 验证：**

| 路径 | 单元/隔离测试 | 生产 E2E |
|------|--------------|----------|
| `buyGas` operator 预扣 | `OpStackSettlementTest` | ✅ `TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success` |
| `refundIsthmusOperatorCost` | `RefundIsthmusTest` | ✅ 经 settlement 链间接验证 |
| OperatorFeeVault 路由 | `OpStackSettlementTest` | ✅ TE fixture `OP_OPERATOR_FEE_RECIPIENT balance > 0` |
| receipt `operatorFee` | `OpStackExecuteViaHostSmokeTest` | ✅ TE `receipt->operatorFee() != 0x0` |

**对照 op-geth：** Isthmus 激活时 operator fee 计入 balance check 与 settlement — 生产路径现已可达 ✅。

---

## Part 1 — 合规矩阵行（增补 S1）

| 能力 | 层级 | 清单来源 | Matrix 状态 | 审计深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|----------|------|-----------|---------|--------------|---------|------|
| OPStack operator fee (Isthmus) | orchestration | 增补 S1 | explicit | 深审 | ✅ | `isthmus/exec-engine.md` §Operator Fee | `OpStackFee.cpp` 公式 ✅；`OpStackTxExecutor.cpp` buy/refund/route ✅；**`m_isIsthmus` 生产 true** | Isthmus 必须扣/退/路由 | `OpStackFeeTest`, `RefundIsthmusTest`, `OpStackSettlementTest`, `TestOpStackTransactionExecutorFixture::operator_fee_recipient_gets_fee_on_success` | 🟡 协议 receipt 缺 scalar/constant；literal/revert 弱断言 |

---

## Part 2 — 偏离项详情

### ✅ D3-1 生产路径 `m_isIsthmus` 已设（OP-01 闭合）

- **54e17a62c：** `OpStackTransactionExecutorImpl.h:210-211` + `OpStackExecuteViaHost.cpp:79-82`。
- **交叉引用：** `task1-executor-wiring.md` Part 2 ✅。

### D3-2 🟡 Receipt scalar/constant — 编排 meta ✅，协议层仍缺

- **54e17a62c 改进：** `OpStackReceiptMeta` 新增 `operatorFeeScalar`/`operatorFeeConstant`；`opStackExecuteViaHost:254-258` 在 params 非零时写入；`L1AttributesDepositTest` 断言 meta scalar ✅。
- **仍缺：** `OpStackTransactionExecutorImpl::makeReceipt` 仅 `setOperatorFee`，未映射 scalar/constant 至 `protocol::TransactionReceipt`（OP-13 标注为协议层 known gap）。
- **严重度：** 🟡 — spent fee 路由正确；metadata parity 在编排层已部分闭合。

### D3-3 🟡 测试覆盖缺口 — 非 wiring 阻断

| 缺口 | 54e17a62c |
|------|-----------|
| ~~无生产路径断言~~ | ✅ TE `operator_fee_recipient_gets_fee_on_success` |
| ~~无 `opStackExecuteViaHost` E2E~~ | ✅ `OpStackExecuteViaHostSmokeTest::l1_fee_recipient_gets_fee_on_success` 断言 `receiptMeta.operatorFee > 0` |
| operator 域内 deposit 对照 | ✅ Task 4 `DepositNoFeeRoutingTest` |
| insufficient balance（含 operator） | ✅ `OpStackExecuteViaHostSmokeTest::insufficient_balance_fails_before_execution` + TE fixture |
| scalar=0、constant>0 边界 | 🟡 仍仅 `IsthmusOperator_zeroParams` |
| **Literal 精确值** | 🟡 TE/smoke 用 `!= 0x0` / `> 0`，未对照 op-geth 1618 向量 E2E |
| **Revert / hard-failure operator fee** | 🟡 TE `revert_keeps_l1_fee` / `hard_failure_*` 未断言 operator |
| **Receipt meta scalar 在 smoke** | 🟡 `OpStackExecuteViaHostSmokeTest` 未断言 `operatorFeeScalar`（`L1AttributesDepositTest` 有） |

---

## Part 3 — 测试断言审计

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `OpStackFeeTest.cpp` | `IsthmusOperator_gas1618_matchesFixture` | ✅ 有效 | `rollup_cost_test.go:35` | 公式字面一致 |
| `OpStackFeeTest.cpp` | `IsthmusOperator_zeroParams_returnsZero` | ✅ 有效 | 空参数 → 0 | 双零短路 |
| `OpStackFeeTest.cpp` | `LoadOpStackFeeParams_unpacksSlots` | ✅ 有效 | `ExtractOperatorFeeParams` | operator slot 8 子域 |
| `RefundIsthmusTest.cpp` | `RefundIsthmus_refundsLimitMinusUsedCost` | ✅ 有效 | `refundIsthmusOperatorCost` 公式 | 直连 `OpStackTxExecutor` 单元测 |
| `OpStackSettlementTest.cpp` | `Settlement_routesCoinbaseBaseFeeL1AndOperator` | ✅ 有效 | buyGas + refundGas 全路由 | 手动 `m_isIsthmus`（合理：直连 executor） |
| `OpStackSettlementTest.cpp` | `HardFailure_stillRefundsUnusedGas` | ✅ 有效 | 失败仍 settlement + fee 路由 | 同上 |
| `TestOpStackTransactionExecutorFixture.cpp` | `operator_fee_recipient_gets_fee_on_success` | ✅ 有效 | TE E2E operator fee | **新增 OP-01 验收** |
| `OpStackExecuteViaHostSmokeTest.cpp` | `l1_fee_recipient_gets_fee_on_success` | ✅ 有效 | 编排 smoke operator + L1 | `makeIsthmusRevisionConfig` 自动 `m_isIsthmus` |
| `L1AttributesDepositTest.cpp` | user tx after L1 attrs | ✅ 有效 | meta scalar/constant | OP-13 编排层 |

### 失败路径覆盖结论

| 断言项 | 有测试 | 与 op-geth 一致（`m_isIsthmus=true`） |
|--------|--------|--------------------------------------|
| 公式 unit | ✅ | ✅ |
| worst-case buyGas | ✅ Settlement + TE E2E | ✅ |
| limit−used refund | ✅ RefundIsthmus | ✅ |
| spent → vault | ✅ Settlement + TE E2E | ✅ |
| deposit 豁免 | ✅ Task 4 | ✅ |
| 生产 `m_isIsthmus` | ✅ | ✅ |
| receipt spent fee E2E | ✅ | ✅ |
| receipt scalar/constant meta | ✅ L1AttributesDepositTest | ✅ |
| receipt scalar/constant 协议层 | ❌ | 🟡 known gap |
| revert operator fee 保留 | 🟡 弱 | 🟡 未断言 |

### 测试执行（54e17a62c）

```bash
ctest --test-dir build/bcos-evm/test -R 'RefundIsthmus|OpStackSettlement|OpStackFee' --output-on-failure
# 3/3 passed (2026-06-21)
```

---

## 汇总

| 子域 | vs op-geth (Isthmus) | 初审计 | 54e17a62c |
|------|----------------------|--------|-----------|
| 公式 `operatorCostIsthmus` | 一致 | ✅ | ✅ |
| L1Block slot 8 参数解码 | 一致 | ✅ | ✅ |
| buyGas / balance check / refund / vault 路由 | 逻辑一致 | ✅（隔离） | ✅（生产 E2E） |
| **`m_isIsthmus` 生产接线** | FB 默认 false | 🔴 | ✅ |
| Deposit 豁免 | 一致 | ✅ | ✅ |
| Receipt metadata | 仅 spent fee | 🟡 | 🟡 meta 有 scalar/constant；协议层仍缺 |
| 测试 | 单元+编排；无 E2E wiring | 🟡 | 🟡 literal/revert 弱断言 |

**Task 3 状态：** **PASS** — OP-01 闭合；内核/编排/生产 E2E ✅。

**剩余 🟡：** 协议 `TransactionReceipt` 缺 scalar/constant；E2E literal 精确值；revert/hard-failure operator fee 断言；scalar=0/constant>0 边界。

**可裁决计数：** ✅ 5 · 🟡 3 · 🔴 0
