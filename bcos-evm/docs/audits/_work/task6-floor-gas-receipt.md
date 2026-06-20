# Task 6 — Floor Gas (EIP-7623) + Settlement + Receipt Meta 审计笔记

**日期：** 2026-06-20  
**范围：** inventory #8–9、#26；`OpStackFloorGas.*`、`OpStackGasSettlement.h`、`OpStackReceiptMeta.h`、`OpStackExecuteViaHost.cpp`、`OpStackTransactionExecutorImpl::makeReceipt`  
**参考：** op-geth v1.101702.2 @ `e8800cffe` — `core/state_transition.go`（`FloorDataGas`、Prague precheck/settlement、OP fee routing）；`core/types/receipt_opstack.go`（receipt 派生）  
**交叉引用：** ETH audit `_work/task5-eip7623.md`（共享公式）；Task 1 `_work/task1-executor-wiring.md`（`m_isIsthmus` 接线）；Task 4 `_work/task4-deposit.md`（deposit 失败 gasUsed 与 floor 交叉）

---

## Step 1: EIP-7623 公式（OP 路径常量）

| 常量 | FB `OpStackFloorGas.h` | op-geth `params` |
|------|------------------------|------------------|
| 基础 gas | `OP_TX_GAS = 21'000` | `TxGas` |
| 非零字节 token | `OP_TX_TOKEN_PER_NON_ZERO_BYTE = 4` | `TxTokenPerNonZeroByte` |
| 每 token floor 成本 | `OP_TX_COST_FLOOR_PER_TOKEN = 10` | `TxCostFloorPerToken` |

**公式（两者一致）：**

```
tokens = nonzeros × 4 + zeros × 1
floorDataGas = 21000 + tokens × 10
```

op-geth `FloorDataGas`（`state_transition.go:120-133`）与 FB `tryFloorDataGas`（`OpStackFloorGas.cpp:32-41`）字面同族 ✅。Access list / auth tuple **不计入** floor（与 geth Prague 分支一致）。

---

## Step 2: Entry precheck（inventory #8）

### 调用链

```
opStackExecuteViaHost
  → executeEntryChecks (OpStackExecuteViaHost.cpp:38-74)
       1. intrinsicGas vs message.gas  → OOG
       2. executeEntryFloorDataGasCheck(gasLimit, calldata)
            → BelowFloor / GasUintOverflow
       3. message.gas -= intrinsicGas
  → executeMessage (EVM)
```

### 与 op-geth 对照（`state_transition.go:538-563`）

| 步骤 | FB | op-geth |
|------|-----|---------|
| 门控 | **无** `revisionConfig.eip7623` 检查；OP Isthmus 路径恒启用 | `rules.IsPrague` |
| intrinsic | `computeTxIntrinsicGas` + auth tuple gas | `IntrinsicGas(...)` |
| floor 准入 | `gasLimit < floorDataGas` → `OutOfGasLimit` | `msg.GasLimit < floorDataGas` → `ErrFloorDataGas` |
| floor 基准 | 原始 `gasLimit`（非 post-intrinsic） | 原始 `msg.GasLimit` ✅ |
| 错误码 | `EVMC_OUT_OF_GAS` + `OutOfGasLimit` | 独立 `ErrFloorDataGas` |

**Isthmus 判定：** Prague 恒真（`makeIsthmusRevisionConfig().eip7623=true`），无 revision 门控可接受 ✅。若未来 OP profile 回退 pre-Prague，需补门控 🟡。

**deposit tx：** deposit 分支同样走 `executeEntryChecks`（`:133`），与 geth deposit 仍受 Prague floor 约束一致 ✅。

**executeEntryChecks 失败（非 EVM）：** 提前 return，`gasUsed=0`、无 nonce bump — 见 Task 4 🔴（与 floor 准入失败语义叠加）。

---

## Step 3: Settlement / floor top-up（inventory #9）

### FB — `postExecuteGasSettlement`（`OpStackGasSettlement.h:17-48`）

```cpp
peakGasUsed = gasLimit - min(gasLeft, gasLimit);
refund = min(stateRefund, peakGasUsed / 5);
gasRemaining = min(gasLeft + refund, gasLimit);
gasUsed = gasLimit - gasRemaining;
if (floorDataGas > 0 && gasUsed < floorDataGas) {
    gasUsed = min(floorDataGas, gasLimit);
    gasRemaining = gasLimit - gasUsed;
    maxUsedGas = max(peakGasUsed, gasUsed);
}
```

### op-geth — Prague 分支（`state_transition.go:646-661`）

```go
peakGasUsed := st.gasUsed()
st.gasRemaining += st.calcRefund()
if st.gasUsed() < floorDataGas {
    st.gasRemaining = st.initialGas - floorDataGas
}
if peakGasUsed < floorDataGas { peakGasUsed = floorDataGas }
// gasUsed = initialGas - gasRemaining
```

**判定：** refund cap（1/5 peak）+ floor top-up + `maxUsedGas` 抬升与 geth Prague 同族 ✅。

### 接线（`OpStackExecuteViaHost.cpp`）

| 路径 | settlement 时机 | floor 来源 |
|------|-----------------|------------|
| 普通 tx | EVM 后 `:225-231` → `refundGas` | `executeEntryChecks` 写入 `m_floorDataGas` |
| deposit 成功 | `:161-168` | 同上 |
| deposit 失败 | 跳过 settlement；`gasUsed = gasLimit` | floor 不参与 🔴（Task 4） |
| entry 失败 | 无 settlement | — |

**架构 deviation（matrix 标注原因）：** OP 路径使用独立 `OpStackFloorGas` + `OpStackGasSettlement`，**不**走 ETH reference 的 `EthTxGasSettlement.h::finalizeEthereumGasUsed`。行为与 geth 对齐；差异为 orchestration 分层（intentional `deviation` token）。

**与 ETH helper 关系：** `Eip7623.h` / `calcEip7623Components` 在 BCOS/ETH 路径使用；OP 路径复制常量到 `OpStackFloorGas.h`，未共享 helper — 公式等价，存在 drift 风险 🟡。

---

## Step 4: OP fee routing + settlement 交互

普通 tx 顺序（对齐 op-geth `innerExecute` → fee block `:691-733`）：

1. `buyGas` — 预扣 `gasLimit × effectivePrice + l1Cost + operatorCostLimit`
2. `executeEntryChecks` + `executeMessage`
3. `postExecuteGasSettlement` — **floor 可能抬高 `gasUsed`**
4. `refundGas` — 按 **post-settlement** `gasUsed` / `gasRemaining` 路由 coinbase、base fee、L1、operator

op-geth 同样在 refund 后、fee routing 前应用 floor top-up，再用最终 `st.gasUsed()` 计费 ✅。

**Operator fee refund：** FB `refundIsthmusOperatorCost`（`OpStackTxExecutor.cpp:93-108`）与 geth `refundIsthmusOperatorCost`（`:836-846`）同公式：`limitCost(gasLimit) - usedCost(gasUsed)` 退回 sender ✅。

---

## Step 5: Receipt meta（inventory #26）

### FB 结构 — `OpStackReceiptMeta.h`

| 字段 | 填充位置 | 写入 receipt |
|------|----------|--------------|
| `l1Fee` | `opStackExecuteViaHost.cpp:236`（`m_l1CostCharged`） | `makeReceipt`:254-257 `setL1Fee` |
| `operatorFee` | `:237-242` `operatorCostIsthmus(gasUsed, …)` | `makeReceipt`:258-262 `setOperatorFee` |
| `depositNonce` | deposit 分支 `:124` mint 前 nonce | `makeReceipt`:263-267 `setDepositNonce` |

### op-geth 对照

| 字段 | op-geth | FB | 判定 |
|------|---------|-----|------|
| `GasUsed` | `result.UsedGas`（含 floor top-up） | `output.gasUsed` → `createReceipt*` | ✅ 同源 settlement |
| `L1Fee` | block 级 `deriveOPStackFields`（`receipt_opstack.go:40`） | 执行时 `l1CostCharged` | ✅ 值应对齐（同 Fjord 公式） |
| `OperatorFeeScalar/Constant` | `deriveOPStackFields:44-47` | **未暴露**；仅存 computed `operatorFee` | 🟡 JSON-RPC parity 缺口 |
| `L1GasPrice/BlobBaseFee/Scalars` | deriveOPStackFields | 未实现 | 🟡 低优先级（RPC 展示） |
| `DepositNonce` | Regolith+ deposit | ✅ | ✅ |
| `DepositReceiptVersion` | Canyon+ 固定 `1` | **未实现** | 🟡 Isthmus 应设 Canyon 版 |

**🔴 生产接线（Task 1 交叉）：** `OpStackTransactionExecutorImpl::opStackExecuteViaHostTx()` 未设 `input.opTxExecutor.m_isIsthmus = true`。导致：

- `buyGas` 不预扣 operator fee
- `opStackExecuteViaHost.cpp:237` 不填 `receiptMeta.operatorFee`
- `makeReceipt` 无 `operatorFee` 字段

测试路径手动 `m_isIsthmus=true`；executor E2E 未断言 operator fee receipt。

---

## Part 1 rows（可直接粘贴 Part 1 矩阵）

| 能力 | 层级 | 清单来源 | Matrix 状态 | 深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|------|------|-----------|---------|--------------|---------|------|
| EIP-7623 entry precheck | orchestration | matrix #8 | explicit | 深审 | ✅ | EIP-7623 §Specification | `OpStackFloorGas.cpp` + `executeEntryChecks` | `FloorDataGas` + `GasLimit` check | `OpStackFloorGasTest` | 无 E2E orchestration 用例；错误码非 `ErrFloorDataGas` |
| EIP-7623 settlement / floor gas | orchestration | matrix #9 | deviation | 深审 | ✅ | EIP-7623 post-refund floor | `OpStackGasSettlement.h` | Prague `:650-661` | `CalcRefundTest` | 无 E2E receipt floor 断言；与 `Eip7623.h` 未共享 |
| OPStack receipt metadata | orchestration | matrix #26 | explicit | 深审 | 🟡 | Isthmus operator fee + OP receipt | `OpStackReceiptMeta` + `makeReceipt` | `receipt_opstack.go` | `OpStackSettlementTest` smoke | operatorFee 生产未接线；缺 scalar/constant；缺 `DepositReceiptVersion` |

---

## Part 2 — 偏离项详情

### 🟡 Receipt operator fee 表示法

**位置：** `OpStackReceiptMeta.h`；`OpStackTransactionExecutorImpl.h:258-262`

**FB：** 存 `operatorFee` = `operatorCostIsthmus(gasUsed)`（wei 金额）。

**op-geth：** receipt 存 `OperatorFeeScalar` + `OperatorFeeConstant`；客户端自行计算。无 `operatorFee` 金额字段。

**影响：** JSON-RPC 字段名/结构不完全 parity；链上余额路由仍正确（若 `m_isIsthmus` 已设）。

---

### 🟡 未共享 `Eip7623.h` helper

**位置：** `OpStackFloorGas.cpp` vs `eth/gas/Eip7623.h`

**影响：** 常量/公式 drift 风险；当前数值一致。

---

### 🟡 无 E2E floor gas receipt 测试

**位置：** `bcos-evm/test/opstack/`、`TestOpStackTransactionExecutorFixture.cpp`

**缺口：** 无 data-heavy calldata tx 断言 `receipt.gasUsed >= floorDataGas`；`OpStackSettlementTest::HardFailure` 使用 `floor=0`。

---

### 🔴 `m_isIsthmus` 生产未接线（Task 1）

**位置：** `OpStackTransactionExecutorImpl.h:210` vs 测试手动设置

**影响：** operator fee buy/refund/receipt 在生产 executor 路径失效。见 Task 1。

---

### 🟡 低优先级 receipt 字段

| 字段 | op-geth | FB |
|------|---------|-----|
| `DepositReceiptVersion` | Canyon+ = 1 | 未设 |
| `L1GasPrice` / scalars | deriveOPStackFields | 未设 |
| `L1GasUsed` | deprecated Fjord+ | 未设（Task 2 已注） |

Isthmus spec 未强制全部字段；RPC 兼容性缺口。

---

## Part 3 — 测试断言审计

### `OpStackFloorGasTest.cpp`

| 用例 | 断言 | 金标准 | 判定 |
|------|------|--------|------|
| `FloorDataGas_emptyCalldata_is21000` | 21000 | geth empty | ✅ |
| `FloorDataGas_zeroBytesOnly` | 21000 + 100×10 | token=1/byte | ✅ |
| `FloorDataGas_nonZeroBytesOnly` | 21000 + 100×4×10 | geth nz×4 | ✅ |
| `FloorDataGas_mixedZeroAndNonZero` | 50 + 50×4 tokens | 混合 | ✅ |
| `FloorDataGas_overflow_returnsError` | `GasUintOverflow` | geth `ErrGasUintOverflow` | ✅ |
| `ExecuteEntryFloorCheck_*` | below/at floor | precheck 语义 | ✅ |

### `CalcRefundTest.cpp`

| 用例 | 断言 | 金标准 | 判定 |
|------|------|--------|------|
| `Settlement_capBinds` | refund = peak/5 | EIP-3529 cap | ✅ |
| `Settlement_floorDataGasBumpsGasUsed` | used=700, limit=1618 | geth floor top-up | ✅ |
| `EvmoneParity_noDoubleCount` | gasLeft 不 double-count | evmone 语义 | ✅ |

### `OpStackSettlementTest.cpp`

| 用例 | 断言 | 判定 |
|------|------|------|
| `Settlement_routesCoinbaseBaseFeeL1AndOperator` | 四方余额 + buy/refund 链 | ✅ fee routing（手动 `m_isIsthmus`） |
| `HardFailure_stillRefundsUnusedGas` | OOG 后仍路由 L1/operator/coinbase | ✅；**floor=0** 🟡 |

**缺口：** 未断言 `receiptMeta`；未覆盖 floor bump 后 operator fee 按抬高 `gasUsed` 计费。

### `OpStackExecuteViaHostSmokeTest.cpp` / `TestOpStackTransactionExecutorFixture.cpp`

| 覆盖 | 判定 |
|------|------|
| `receiptMeta.l1Fee` on success | ✅ smoke |
| `receipt.l1Fee()` on revert E2E | ✅ fixture |
| floor gas receipt | ❌ 无 |
| `operatorFee` receipt E2E | ❌ 无（且生产 `m_isIsthmus` 未设） |
| `depositNonce` on receipt | 🟡 fixture 未断言 |

---

## Step 6 — 判定汇总

| Inventory | 能力 | 状态 | 说明 |
|-----------|------|------|------|
| #8 | entry precheck | ✅ | 公式 + gasLimit 准入与 op-geth 一致；Orchestration 恒启用（Isthmus OK） |
| #9 | settlement / floor gas | ✅ | `postExecuteGasSettlement` 与 geth Prague 同族；独立 helper 为 intentional deviation |
| #26 | receipt metadata | 🟡 | l1Fee/depositNonce ✅；operatorFee 生产失效 + 字段 parity 缺口 |

**Part 3 测试断言：** 🟡 — 单元测试充分；缺 OP orchestration E2E floor receipt + operator fee receipt。

**P0（跨 Task）：** 修复 `m_isIsthmus` 生产接线（Task 1）。  
**P1 补测：** data-heavy tx → `gasUsed == floorDataGas` receipt；operator fee receipt E2E；可选 `DepositReceiptVersion`。
