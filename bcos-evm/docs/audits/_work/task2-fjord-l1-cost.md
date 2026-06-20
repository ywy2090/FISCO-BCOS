# Task 2 — Rollup Cost + L1 Data Fee (Fjord) 审计笔记

**日期：** 2026-06-20  
**范围：** `RollupCost.*`、`OpStackFee.*`（Fjord L1 部分）、`OpStackConstants.h`；executor `buildRollupCostData` 接线（影响 E2E）  
**参考：** op-geth `core/types/rollup_cost.go`；optimism/specs `protocol/fjord/exec-engine.md`（pin `689a96f6`）

---

## Part 1 rows（可直接粘贴 Part 1 矩阵）

| 能力 | 层级 | 清单来源 | Matrix 状态 | 深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|------|------|-----------|---------|--------------|---------|------|
| Fjord L1 cost 常量 | orchestration | 增补（L1 data fee） | 待 matrix 合入 | 深审 | ✅ | `fjord/exec-engine.md` intercept/fastlzCoef/minTxSize | `OpStackConstants.h:37-40` | `rollup_cost.go:92-96` | 间接（`OpStackFeeTest` 公式链） | — |
| FastLZ 压缩长度估算 (`flzCompressLen`) | orchestration | 增补 | 待 matrix 合入 | 深审 | ✅ | `fjord/exec-engine.md` FastLZ 实现 MUST | `RollupCost.cpp:12-105` | `rollup_cost.go:667-743` | `RollupCostTest` 5 向量 | — |
| RollupCostData 构建 (`newRollupCostData`) | orchestration | 增补 | 待 matrix 合入 | 深审 | ✅ | spec: 对 tx payload 计 zero/one + FastLZ | `RollupCost.cpp:113-128` | `rollup_cost.go:137-147` | `RollupCostTest::NewRollupCostData_countsBytesAndFastLz` | zeroes/ones 在 Fjord 公式中未消费（与 op-geth 一致） |
| Fjord L1 fee 公式 (`l1CostFjord`) | orchestration | 增补 | 待 matrix 合入 | 深审 | ✅ | `fjord/exec-engine.md` pseudocode L25-28 | `OpStackFee.cpp:36-56` | `rollup_cost.go:607-627` | `OpStackFeeTest::FjordL1_emptyTx_matches3203000` | 无 min-bound / parity 边界用例 |
| 空 RollupCostData 不收费 | orchestration | 增补 | 待 matrix 合入 | 深审 | ✅ | deposit / 无 payload 场景 | `OpStackFee.cpp:38-41` `isEmpty()` | `rollup_cost.go:196-198` 返回 nil | `OpStackFeeTest::FjordL1_emptyRollupCostData_returnsZero` | — |
| L1Block fee scalar 读取 | orchestration | 增补 | 待 matrix 合入 | 深审 | ✅ | Ecotone scalars slot layout | `OpStackFee.cpp:70-94` `kScalarSectionStart=16` | `rollup_cost.go:649-654` `scalarSectionStart` | `OpStackFeeTest::LoadOpStackFeeParams_unpacksSlots` | — |
| RollupCost tx 字节源 (E2E) | executor-integration | 增补 S3 交叉 | 待 matrix 合入 | 深审 | 🔴 | spec: **RLP-encoded signed tx** | `OpStackTxInputBuilder.h:109-117` 用 `extraTransactionBytes`（= `encodeForSign`，无 v/r/s）或 fallback `tx.input()` | `transaction.go:405-409` `MarshalBinary()` | 无 E2E 字节源断言 | 运行时 FastLZ 输入与 op-geth 不一致 |
| Fjord L1 公式 E2E 接线 | orchestration | 增补 | 待 matrix 合入 | 深审 | ✅ | Isthmus 使用 Fjord 路径 | `OpStackExecuteViaHost.cpp:87-90` | `NewL1CostFunc` Fjord 分支 | `OpStackExecuteViaHostSmokeTest`（mock rollupCostData） | smoke 未用真实 serialized tx |

---

## Part 2 deviations

### 🔴 RollupCost 输入字节非 signed RLP tx（E2E）

**位置：** `transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h:109-117`

**FB：**
```cpp
if (!extra.empty()) return newRollupCostData(extra);  // extra = encodeForSign()
return newRollupCostData(tx.input());                 // fallback = calldata only
```

**op-geth：** `tx.MarshalBinary()` → 含 type/RLP payload **+ 签名 v,r,s**（`transaction.go:405-409`）

**spec：** `fastlzSize` = FastLZ-compressed **RLP-encoded signed tx**（`fjord/exec-engine.md` L38）

**Web3 入库路径：** `Web3Transaction::takeToTarsTransaction()` 将 `encodeForSign()` 写入 `extraTransactionBytes`，签名存于独立 `signature` 字段（`Web3Transaction.cpp:164-173`），未重组为 Ethereum `MarshalBinary`。

**影响：** 单元测试用 op-geth `empty_tx.bin`（完整 signed bytes）验证公式 ✅，但 executor 运行时 `flzCompressLen` / `estimatedSize` / `l1Fee` 可能与 op-geth 链上计费不一致。legacy / typed tx 均受影响。

**修复方向：** 在 TE 层实现 `marshalBinary(tx)`（RLP + 签名）再喂给 `newRollupCostData`；或扩展 `buildRollupCostData` 拼接 `signature` 为 Ethereum 编码。

---

### 🟡 测试覆盖缺口（公式本身已对齐）

| 缺口 | op-geth 对照 | 严重度 |
|------|--------------|--------|
| 无 `TestFjordL1CostFuncMinimumBounds` 等价 | `rollup_cost_test.go:67-99`（fastLz 100/150/170 → min fee 3_203_000；171+ 递增） | 🟡 |
| 无 `TestFjordL1CostSolidityParity` 等价 | `rollup_cost_test.go:102-117`（fee=105484, gas=2463） | 🟡 |
| 无 contract-call tx L1 cost 端到端 | `TestFlzCompressLen` contract vector fastLz=202 仅测压缩长度 | 🟡 |
| `OpStackExecuteViaHostSmokeTest` 注入 synthetic `{ones:2, fastLzSize:3}` | 未验证 `buildRollupCostData` → `l1CostFjord` 链 | 🟡 |

---

### 🟡 L1GasUsed / calldataGasUsed 未暴露（低优先级）

op-geth `NewL1CostFuncFjord` 第二返回值 `calldataGasUsed = estimatedSize * 16 / 1e6`（`rollup_cost.go:623-624`），供 receipt `L1GasUsed`（已 deprecated，`fjord/exec-engine.md` L73-75）。

FB `l1CostFjord` 仅返回 fee；`OpStackReceiptMeta` 只有 `l1Fee` 字段。与 Isthmus spec 不冲突，但与 op-geth receipt 字段不完全 parity。

---

## Part 3 test assertion notes

### `RollupCostTest.cpp`

| 用例 | 断言 | 金标准 | 判定 |
|------|------|--------|------|
| `FlzCompressLen_matchesOpGethVectors` | empty→0; 1000×0x01→21; 1000×0x00→21; emptyTx→31; contractCall→202 | `rollup_cost_test.go:474-512` 同向量 | ✅ 字面一致 |
| `NewRollupCostData_countsBytesAndFastLz` | emptyTx: zeroes=0, ones=30, fastLz=31 | op-geth emptyTx `NewRollupCostData` | ✅ |

**备注：** `kEmptyTxBytes` hex 与 op-geth `emptyTx.MarshalBinary()` 注释一致（`RollupCostTest.cpp:19-20`）。

### `OpStackFeeTest.cpp`（Task 2 相关用例）

| 用例 | 断言 | 金标准 | 判定 |
|------|------|--------|------|
| `FjordL1_emptyTx_matches3203000` | fastLz=31 → cost=3_203_000 | `rollup_cost_test.go:34` `fjordFee`; 公式 `100_000_000 × (2×1000×1e6×16 + 3×10×1e6) / 1e12` | ✅ |
| `FjordL1_emptyRollupCostData_returnsZero` | empty struct → 0 | op-geth nil/0 for `{}` | ✅ |
| `LoadOpStackFeeParams_unpacksSlots` | L1 base/blob fee + scalars + operator slots | `ExtractEcotoneFeeParams` / `ExtractOperatorFeeParams` | ✅（L1 部分 Task 2；operator 属 Task 3） |

**Out of scope（同文件）：** `IsthmusOperator_*` → Task 3。

### 常量逐项对照

| 常量 | FB (`OpStackConstants.h`) | op-geth (`rollup_cost.go`) | Match |
|------|---------------------------|----------------------------|-------|
| intercept | `-42'585'600` | `L1CostIntercept = -42_585_600` | ✅ |
| fastlz coef | `836'500` | `L1CostFastlzCoef = 836_500` | ✅ |
| min tx scaled | `100'000'000` (= 100 × 1e6) | `MinTransactionSizeScaled` | ✅ |
| divisor | `1'000'000'000'000` | `fjordDivisor` | ✅ |
| scalar slot offset | `32-12-4 = 16` | `scalarSectionStart = 16` | ✅ |
| Isthmus L1 attrs len | `176` | `IsthmusL1AttributesLen = 176` | ✅ |

### `l1CostFjord` 公式对照

**Spec / op-geth：**
```
l1FeeScaled = baseFeeScalar*l1BaseFee*16 + blobFeeScalar*l1BlobBaseFee
estimatedSizeScaled = max(100*1e6, intercept + fastlzCoef*fastLzSize)
l1Fee = estimatedSizeScaled * l1FeeScaled / 1e12
```

**FB（`OpStackFee.cpp:43-55`）：** 结构等价；`estimatedSize` 用 `s256` 处理负 intercept 后 clamp，再转 `u256` 乘除。empty tx（fastLz=31）回归值 `-16654100 < 100_000_000` → clamp → **3_203_000** ✅。

### 测试执行

```bash
ctest --test-dir build/bcos-evm/test -R 'RollupCost|OpStackFee' --output-on-failure
# 2/2 passed (2026-06-20)
```

---

## 算法对照摘要

### `flzCompressLen`

`RollupCost.cpp:12-105` 为 op-geth `FlzCompressLen`（`rollup_cost.go:667-743`）的逐行 port：8192 槽 hash table、`2654435769` 乘子、`0x1fff` mask、literal/match 分支逻辑一致。Go `uint32` vs C++ `uint32_t` 无符号语义等价。

### `newRollupCostData`

逐字节 zero/one 计数 + `flzCompressLen(serializedTx)`，与 `NewRollupCostData` 一致。`isEmpty()` 三字段全零 → `l1CostFjord` 返回 0，对齐 op-geth deposit / 空 payload 行为。

---

## Summary

| 指标 | 值 |
|------|-----|
| Part 1 行数 | 8 |
| ✅ | 7 |
| 🟡 | 0（行级；Part 2 有 2 项 🟡 测试/L1GasUsed 缺口） |
| 🔴 | 1（RollupCost tx 字节源 E2E） |
| 📋 | 0 |
| ⚪ | 0 |

### Top findings

1. **🔴 E2E 字节源：** `buildRollupCostData` 使用 `encodeForSign` / `tx.input()`，非 op-geth `MarshalBinary()` signed tx；spec MUST 要求 signed RLP。核心 `flzCompressLen` / `l1CostFjord` 在正确输入下与 op-geth 一致，但 production 路径可能算错 L1 fee。
2. **✅ 公式与常量：** 四项 Fjord 常量、FastLZ 算法、`l1CostFjord` 线性回归 + clamp + 除法与 op-geth / optimism spec 完全一致；canonical empty-tx fee **3_203_000** 有测试锁定。
3. **🟡 测试深度不足：** 缺少 op-geth 的 minimum-bounds 阶梯（fastLz 100–200）与 Solidity parity 向量；无真实 serialized tx 的 executor→fee E2E 断言。
