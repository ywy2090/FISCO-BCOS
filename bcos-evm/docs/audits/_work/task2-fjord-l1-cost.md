# Task 2 — Rollup Cost + L1 Data Fee (Fjord) 审计笔记

**日期：** 2026-06-21（复审计 @ `54e17a62c`）  
**初审计：** 2026-06-20 @ `f989f073f` — 🔴 signed RLP E2E 字节源  
**范围：** `RollupCost.*`、`OpStackFee.*`（Fjord L1 部分）、`OpStackConstants.h`；executor `buildRollupCostData` 接线（影响 E2E）  
**参考：** op-geth `core/types/rollup_cost.go`；optimism/specs `protocol/fjord/exec-engine.md`（pin `689a96f6`）

---

## Part 1 rows（可直接粘贴 Part 1 矩阵）

| 能力 | 层级 | 清单来源 | Matrix 状态 | 深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|------|------|-----------|---------|--------------|---------|------|
| Fjord L1 cost 常量 | orchestration | 增补（L1 data fee） | explicit | 深审 | ✅ | `fjord/exec-engine.md` intercept/fastlzCoef/minTxSize | `OpStackConstants.h:37-40` | `rollup_cost.go:92-96` | 间接（`OpStackFeeTest` 公式链） | — |
| FastLZ 压缩长度估算 (`flzCompressLen`) | orchestration | 增补 | explicit | 深审 | ✅ | `fjord/exec-engine.md` FastLZ 实现 MUST | `RollupCost.cpp:12-105` | `rollup_cost.go:667-743` | `RollupCostTest` 5 向量 | — |
| RollupCostData 构建 (`newRollupCostData`) | orchestration | 增补 | explicit | 深审 | ✅ | spec: 对 tx payload 计 zero/one + FastLZ | `RollupCost.cpp:113-128` | `rollup_cost.go:137-147` | `RollupCostTest::NewRollupCostData_countsBytesAndFastLz` | zeroes/ones 在 Fjord 公式中未消费（与 op-geth 一致） |
| Fjord L1 fee 公式 (`l1CostFjord`) | orchestration | 增补 | explicit | 深审 | ✅ | `fjord/exec-engine.md` pseudocode L25-28 | `OpStackFee.cpp:36-56` | `rollup_cost.go:607-627` | `OpStackFeeTest::FjordL1_emptyTx_matches3203000` + min-bound 向量 + **FIX-04** `FIX04_FjordL1CostSolidityParity_matchesOpGeth`（fee=105484, calldataGasUsed=2463 @ op-geth `e8800cffe`） | — |
| 空 RollupCostData 不收费 | orchestration | 增补 | explicit | 深审 | ✅ | deposit / 无 payload 场景 | `OpStackFee.cpp:38-41` `isEmpty()` | `rollup_cost.go:196-198` 返回 nil | `OpStackFeeTest::FjordL1_emptyRollupCostData_returnsZero` | — |
| L1Block fee scalar 读取 | orchestration | 增补 | explicit | 深审 | ✅ | Ecotone scalars slot layout | `OpStackFee.cpp:70-94` `kScalarSectionStart=16` | `rollup_cost.go:649-654` `scalarSectionStart` | `OpStackFeeTest::LoadOpStackFeeParams_unpacksSlots` | — |
| RollupCost tx 字节源 (E2E) | executor-integration | 增补 S3 交叉 | explicit | 深审 | ✅ | spec: **RLP-encoded signed tx** | `OpStackTxInputBuilder.h:110-128` `encodeWeb3SignedMarshalBinary`；`Web3SignedTxEncoder.h:140+` | `transaction.go:405-409` `MarshalBinary()` | `OpStackTxInputBuilderTest::buildRollupCostData_uses_signed_web3_rlp_not_encodeForSign` | — |
| Fjord L1 公式 E2E 接线 | orchestration | 增补 | explicit | 深审 | ✅ | Isthmus 使用 Fjord 路径 | `OpStackExecuteViaHost.cpp:87-90`；`OpStackTransactionExecutorImpl.h:207` | `NewL1CostFunc` Fjord 分支 | `TestOpStackTransactionExecutorFixture::FIX05_signed_rlp_rollup_execute_e2e` | — |

---

## Part 2 deviations

### ✅ RollupCost 输入字节已改为 signed RLP tx（OP-02 闭合）

**初审计（f989f073f）：** `buildRollupCostData` 使用 `encodeForSign()` / `tx.input()`，非 op-geth `MarshalBinary()`。

**54e17a62c 验证：**

1. **`encodeWeb3SignedMarshalBinary`** — `Web3SignedTxEncoder.h:140+` 从 `extraTransactionBytes`（unsigned RLP）+ `signatureData` 重组 Ethereum signed tx bytes。
2. **`buildRollupCostData` 分支** — `OpStackTxInputBuilder.h:110-128`：
   - deposit tx（type `0x7E`）→ 直接用 extra bytes ✅
   - Web3 tx → `encodeWeb3SignedMarshalBinary` → `newRollupCostData` ✅
   - fallback → extra 或 `tx.input()`
3. **生产接线** — `OpStackTransactionExecutorImpl.h:207` `buildRollupCostData(m_transaction)` 在 TE 路径调用。

**测试：** `OpStackTxInputBuilderTest::buildRollupCostData_uses_signed_web3_rlp_not_encodeForSign` 断言 builder 输出与 golden signed RLP 的 `fastLzSize/ones/zeroes` 一致，且与 unsigned 输入不同 ✅。

---

### 🟡 测试覆盖缺口（公式本身已对齐）

| 缺口 | op-geth 对照 | 严重度 | 54e17a62c |
|------|--------------|--------|-----------|
| ~~无 minimum-bounds 阶梯~~ | `rollup_cost_test.go:67-99` | — | ✅ `OpStackFeeTest::FjordL1_minimumBounds_*` |
| ~~无 `TestFjordL1CostSolidityParity` 等价~~ | `rollup_cost_test.go:102-117` @ `e8800cffe`（fee=105484, gas=2463） | — | ✅ **FIX-04** `OpStackFeeTest::FIX04_FjordL1CostSolidityParity_matchesOpGeth` |
| contract-call tx L1 cost | `TestFlzCompressLen` fastLz=202 | 🟡 | ✅ 压缩长度 + `FjordL1_contractCallShape_fastLz202_matchesFormula`（fee=4_048_188） |
| `OpStackExecuteViaHostSmokeTest` synthetic rollupCostData | 未验证真实 serialized tx → fee | 🟡 | 仍用 `{ones:2, fastLzSize:3}` |
| ~~TE E2E 未断言 `buildRollupCostData` → L1 fee 字面~~ | `TestOpStackTransactionExecutorFixture::FIX05_signed_rlp_rollup_execute_e2e` | — | ✅ **FIX-05** signed RLP → fastLz → `l1CostFjord` → receipt + recipient delta |

---

### 🟡 L1GasUsed / calldataGasUsed 未暴露（低优先级）

op-geth `NewL1CostFuncFjord` 第二返回值 `calldataGasUsed = estimatedSize * 16 / 1e6`（`rollup_cost.go:623-624`），供 receipt `L1GasUsed`（已 deprecated，`fjord/exec-engine.md` L73-75）。

FB `l1CostFjord` 仅返回 fee；`OpStackReceiptMeta` 只有 `l1Fee` 字段。与 Isthmus spec 不冲突，但与 op-geth receipt 字段不完全 parity。未变。

---

## Part 3 test assertion notes

### `RollupCostTest.cpp`

| 用例 | 断言 | 金标准 | 判定 |
|------|------|--------|------|
| `FlzCompressLen_matchesOpGethVectors` | empty→0; 1000×0x01→21; 1000×0x00→21; emptyTx→31; contractCall→202 | `rollup_cost_test.go:474-512` 同向量 | ✅ 字面一致 |
| `NewRollupCostData_countsBytesAndFastLz` | emptyTx: zeroes=0, ones=30, fastLz=31 | op-geth emptyTx `NewRollupCostData` | ✅ |

### `OpStackFeeTest.cpp`（Task 2 相关用例）

| 用例 | 断言 | 金标准 | 判定 |
|------|------|--------|------|
| `FjordL1_emptyTx_matches3203000` | fastLz=31 → cost=3_203_000 | `rollup_cost_test.go:34` `fjordFee` | ✅ |
| `FjordL1_emptyRollupCostData_returnsZero` | empty struct → 0 | op-geth nil/0 for `{}` | ✅ |
| `LoadOpStackFeeParams_unpacksSlots` | L1 base/blob fee + scalars + operator slots | `ExtractEcotoneFeeParams` / `ExtractOperatorFeeParams` | ✅（L1 部分 Task 2） |
| `FjordL1_minimumBounds_clampsBelowMinTxSize` | fastLz 100/150/170 → 3_203_000 | `rollup_cost_test.go:67-99` | ✅ **新增 OP-11** |
| `FjordL1_minimumBounds_fastLz171_exceedsMinFee` | fastLz=171 > min fee | 同上 | ✅ **新增 OP-11** |
| `FjordL1_contractCallShape_fastLz202_matchesFormula` | fastLz=202 → 4_048_188 | 公式回归 | ✅ **新增 OP-11** |
| `FIX04_FjordL1CostSolidityParity_matchesOpGeth` | fastLz=235 → fee=105484, calldataGasUsed=2463, estimatedSizeScaled=153_991_900 | `rollup_cost_test.go:102-117` @ `e8800cffe` `TestFjordL1CostSolidityParity` | ✅ **FIX-04 ADR-012 Task 4** |

**Out of scope（同文件）：** `IsthmusOperator_*` → Task 3。

### `OpStackTxInputBuilderTest.cpp`（Task 2 E2E 字节源）

| 用例 | 断言 | 判定 |
|------|------|------|
| `buildRollupCostData_uses_signed_web3_rlp_not_encodeForSign` | builder == golden signed RLP；≠ unsigned | ✅ **新增 OP-02** |
| `buildRollupCostData_deposit_uses_extra_bytes_unchanged` | deposit extra 直通 | ✅ |

### `TestOpStackTransactionExecutorFixture.cpp`（Task 2 TE E2E）

| 用例 | 断言 | 判定 |
|------|------|------|
| `FIX05_signed_rlp_rollup_execute_e2e` | signed RLP `buildRollupCostData` ≠ unsigned `encodeForSign`；`l1CostFjord` 字面 → `receipt.l1Fee` + `OP_L1_FEE_RECIPIENT` balance delta | ✅ **FIX-05 ADR-012 Task 5** |
| `l1_fee_recipient_gets_fee_on_success` | unsigned `tx.input()` fallback fee-routing smoke（superseded for R4/D2-2 by FIX-05 case） | ✅ smoke |

### 常量逐项对照

| 常量 | FB (`OpStackConstants.h`) | op-geth (`rollup_cost.go`) | Match |
|------|---------------------------|----------------------------|-------|
| intercept | `-42'585'600` | `L1CostIntercept = -42_585_600` | ✅ |
| fastlz coef | `836'500` | `L1CostFastlzCoef = 836_500` | ✅ |
| min tx scaled | `100'000'000` (= 100 × 1e6) | `MinTransactionSizeScaled` | ✅ |
| divisor | `1'000'000'000'000` | `fjordDivisor` | ✅ |
| scalar slot offset | `32-12-4 = 16` | `scalarSectionStart = 16` | ✅ |
| Isthmus L1 attrs len | `176` | `IsthmusL1AttributesLen = 176` | ✅ |

### 测试执行（54e17a62c）

```bash
ctest --test-dir build/bcos-evm/test -R 'RollupCost|OpStackFee|OpStackTxInputBuilder' --output-on-failure
# 3/3 passed (2026-06-21)
```

---

## Summary

| 指标 | 初审计 | 54e17a62c |
|------|--------|-----------|
| Part 1 行数 | 8 | 8 |
| ✅ | 7 | **8** |
| 🟡 | 0（行级） | **0（行级；Part 2 有 3 项 🟡）** |
| 🔴 | 1 | **0** |

**Task 2 状态：** **PASS** — OP-02 闭合；公式/常量/FastLZ/字节源与 op-geth 对齐。

**剩余 🟡：** `L1GasUsed` receipt 字段；`OpStackExecuteViaHostSmokeTest` synthetic rollup smoke。
