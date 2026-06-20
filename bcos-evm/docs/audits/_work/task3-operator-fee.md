# Task 3 — OPStack Operator Fee (Isthmus) 审计笔记

**日期：** 2026-06-20  
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
| Receipt 扩展：scalar/constant 非零时写入 receipt | §Receipts | op-geth receipt 类型扩展（FB 仅写 spent `operatorFee` 金额） |

**Isthmus 审计锚点：** operator fee 自 Isthmus 起生效；Fjord L1 fee 与之独立叠加（Task 2）。Jovian operator-fee-fix 公式 **不在** Isthmus 审计范围。

---

## Step 2: FB 实现对照

### 公式与参数加载（`OpStackFee.cpp`）

| 项 | FB | op-geth | 一致 |
|----|----|---------|------|
| Isthmus 公式 | `operatorCostIsthmus:65-67` | `newOperatorCostFuncIsthmus` | ✅ |
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

### 编排接线（`OpStackExecuteViaHost.cpp`）

| 项 | 行为 | 备注 |
|----|------|------|
| `loadOpStackFeeParams` + `m_operatorCostFunc` | 始终绑定（`:87-93`） | 函数存在 |
| buyGas / refund 门控 | **`m_isIsthmus && m_operatorCostFunc`**（`OpStackTxExecutor.cpp:60,95,137`） | 依赖调用方设 flag |
| receipt `operatorFee` | 仅 `m_isIsthmus` 时写入 spent fee（`:237-242`） | 同门控 |

---

## Step 3: 🔴 Task 1 交叉引用 — `m_isIsthmus` 生产未接线

**Task 1 结论（S3）：** `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()`（`:197-210`）**未**设置 `input.opTxExecutor.m_isIsthmus = true`。

**对本 Task 的影响：**

| 路径 | 单元/隔离测试 | 生产 E2E |
|------|--------------|----------|
| `buyGas` operator 预扣 | 测试手动 `m_isIsthmus=true` | **永不执行** |
| `refundIsthmusOperatorCost` | 同上 | **永不执行** |
| OperatorFeeVault 路由 | `OpStackSettlementTest` 手动设 flag | **永不执行** |
| receipt `operatorFee` | smoke 未断言 | **永不写入** |

`opStackExecuteViaHost` 虽绑定 `m_operatorCostFunc`，但 **所有扣费/退还/路由** 均二次门控 `m_isIsthmus`。生产 Isthmus 链 operator fee **端到端失效** — 与 op-geth `IsOptimismIsthmus` 行为 🔴 偏离。

**修复建议（与 Task 1 一致）：** 在 `opStackExecuteViaHostTx()` 增加 `input.opTxExecutor.m_isIsthmus = true`（或 timestamp/fork helper）。

**严重度：** 🔴 — 阻塞 Isthmus operator fee 合规；公式/单元正确但 orchestration 未激活。

---

## Part 1 — 合规矩阵行（增补 S1）

| 能力 | 层级 | 清单来源 | Matrix 状态 | 审计深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|----------|------|-----------|---------|--------------|---------|------|
| OPStack operator fee (Isthmus) | orchestration | 增补 S1 | 待 matrix 合入 | 深审 | 🔴 **BLOCKED_ON_WIRING** | `isthmus/exec-engine.md` §Operator Fee | `OpStackFee.cpp` 公式 ✅；`OpStackTxExecutor.cpp` buy/refund/route ✅；**`m_isIsthmus` 生产 false** | Isthmus 必须扣/退/路由；`NewOperatorCostFunc` + `state_transition` | `OpStackFeeTest`, `RefundIsthmusTest`, `OpStackSettlementTest` | **Task 1：`m_isIsthmus` 未设**；receipt scalar/constant；E2E 缺测 |

**Matrix patch 建议：** 新增行 `OPStack operator fee (Isthmus) | orchestration | explicit | OpStackFeeTest, RefundIsthmusTest, OpStackSettlementTest`。

---

## Part 2 — 偏离项详情

### D3-1 🔴 生产路径 `m_isIsthmus` 未设 → operator fee 全链路失效

- **现象：** `OpStackTransactionExecutorImpl.h:197-210` 未设 `m_isIsthmus`；默认 `false`（`OpStackTxExecutor.h:28`）。
- **影响：** buyGas 不预扣、不校验 balance；refund 不退 operator 差额；OperatorFeeVault 无入账；receipt 无 `operatorFee`。
- **规范：** specs §EVM Fee Semantics MUST #1–#4。
- **金标准：** op-geth `IsOptimismIsthmus` + `OperatorCostFunc` 非 nil。
- **修复：** 见 Task 1 S3；本 Task 不重复实现。
- **交叉引用：** `task1-executor-wiring.md` Part 2。

### D3-2 🟡 Receipt 未写入 `operatorFeeScalar` / `operatorFeeConstant`

- **现象：** `OpStackReceiptMeta` 仅 `operatorFee`（spent）；`makeReceipt` 只 `setOperatorFee`（`OpStackTransactionExecutorImpl.h:258-261`）。specs §Receipts 要求非零参数时 receipt 含 scalar/constant 字段。
- **金标准：** optimism specs + op-geth receipt 扩展。
- **严重度：** 🟡 — spent fee 路由正确时仍缺 metadata parity；依赖 D3-1 修复后验证。

### D3-3 🟡 测试覆盖缺口 — 生产 wiring / E2E / deposit 隔离

| 缺口 | 说明 |
|------|------|
| 无生产路径断言 | 所有 opstack fee 测试手动 `m_isIsthmus=true` |
| 无 `opStackExecuteViaHost` E2E | 未用 `loadOpStackFeeParams` + 真实 L1Block slot 8 跑完整 buy→execute→refund |
| operator 域内无 deposit 对照 | deposit 豁免在 `DepositNoFeeRoutingTest`（Task 4），本 Task 测试未覆盖 |
| 无 insufficient balance（含 operator） | buyGas 失败路径未单测 |
| scalar=0、constant>0 边界 | 仅 `IsthmusOperator_zeroParams` |

---

## Part 3 — 测试断言审计

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `OpStackFeeTest.cpp` | `IsthmusOperator_gas1618_matchesFixture` | ✅ 有效 | `rollup_cost_test.go:35` `ithmusOperatorFee` | 公式字面一致 |
| `OpStackFeeTest.cpp` | `IsthmusOperator_zeroParams_returnsZero` | ✅ 有效 | 空参数 → 0 | 双零短路 |
| `OpStackFeeTest.cpp` | `LoadOpStackFeeParams_unpacksSlots` | ✅ 有效 | `ExtractOperatorFeeParams` | operator slot 8 子域 |
| `RefundIsthmusTest.cpp` | `RefundIsthmus_refundsLimitMinusUsedCost` | ✅ 有效 | `refundIsthmusOperatorCost` 公式 | limit=2618, used=1500 → refund 1118；**手动 `m_isIsthmus`** |
| `OpStackSettlementTest.cpp` | `Settlement_routesCoinbaseBaseFeeL1AndOperator` | ✅ 有效 | buyGas + refundGas 全路由 | sender/coinbase/base/L1/operator 余额链正确；**手动 `m_isIsthmus`** |
| `OpStackSettlementTest.cpp` | `HardFailure_stillRefundsUnusedGas` | ✅ 有效 | 失败仍 settlement + fee 路由 | OOG 后 operator vault=430；**手动 `m_isIsthmus`** |
| `TestOpStackTransactionExecutorFixture.cpp` | smoke | 🟡 部分 | E2E operator fee | 未断言 operator 扣费/收据（Task 1） |
| `OpStackExecuteViaHostSmokeTest.cpp` | 各用例 | 🟡 部分 | E2E | 手动 flag；无 operator fee 数值断言 |

### 断言验算（`Settlement_routesCoinbaseBaseFeeL1AndOperator`）

```
effectiveGasPrice = min(5+2, 10) = 7
buyGas debit = 1000×7 + 100(L1) + (1000+10)(op limit) = 8110
refund: sender +600×7 +600(op refund); coinbase +400×5; base +400×2; L1 +100; operator vault +410
→ sender=16690 ✅（与 BOOST_CHECK 一致）
```

### 失败路径覆盖结论

| 断言项 | 有测试 | 与 op-geth 一致（`m_isIsthmus=true`） |
|--------|--------|--------------------------------------|
| 公式 unit | ✅ | ✅ |
| worst-case buyGas | ✅ Settlement buyGas | ✅ |
| limit−used refund | ✅ RefundIsthmus | ✅ |
| spent → vault | ✅ Settlement + HardFailure | ✅ |
| deposit 豁免 | ❌ 本 Task 文件 | ✅（Task 4） |
| 生产 `m_isIsthmus` | ❌ | 🔴 未接线 |
| receipt spent fee E2E | ❌ | 🔴（门控） |
| receipt scalar/constant | ❌ | 🟡 未实现 |

---

## 汇总

| 子域 | vs op-geth (Isthmus) | 严重度 |
|------|----------------------|--------|
| 公式 `operatorCostIsthmus` | 一致 | ✅ |
| L1Block slot 8 参数解码 | 一致 | ✅ |
| buyGas / balance check / refund / vault 路由 | 逻辑一致 | ✅（隔离测试） |
| **`m_isIsthmus` 生产接线** | FB 默认 false；geth Isthmus 必启用 | 🔴 |
| Deposit 豁免 | 一致 | ✅ |
| Receipt metadata | 仅 spent fee；缺 scalar/constant | 🟡 |
| 测试 | 单元+编排正确；无 E2E wiring | 🟡 |

**Task 3 状态：** **BLOCKED_ON_WIRING** — 内核/编排实现 ✅，生产 E2E 🔴（Task 1 S3）

**P0 修复顺序：** D3-1（`m_isIsthmus` 接线，Task 1）→ E2E operator fee 断言 → D3-2 receipt 扩展 → D3-3 补测

**可裁决计数（本 Task 域）：** ✅ 4 · 🟡 3 · 🔴 1
