# ETH Reference / TE — EIP-1559 费用市场结算对齐 — 设计规格

**日期：** 2026-06-21  
**版本：** v1.2（二次审查修订）  
**状态：** 已批准 → writing-plans  
**范围决策：** **方案 B** — shared `eth/gas` 公式 + TE orchestration + EEST adapter 对齐；**不**改 `bcos-framework::effectiveGasPrice()`（方案 C 留后续）  
**前置：** ADR-005（orchestration 边界）、ADR-015（7702/7623 settlement）、`2026-06-21-eth-eest-7702-session-handoff.md`  
**参考实现：** `OpStackTxExecutor::{buyGas,refundGas}`、`ExecuteViaEthAdapter::applyGstTransactionSettlement`

---

## 1. 背景

`bcos-evm/eth/` 当前对 EIP-1559 的处理呈 **三层分裂**：

| 层级 | 现状 | geth 差距 |
|------|------|-----------|
| EVM 运行时 | `block_base_fee` 已注入 `evmc_tx_context`；`BASEFEE` opcode 可用 | ✅ |
| Precheck | `ExecuteViaEthPreCheck` 校验 `gasFeeCap ≥ gasTipCap` 且 `≥ baseFee`（W4） | ✅ |
| `GASPRICE` opcode | `executeMessage` 使用调用方传入的 `input.gasPrice`，常为 legacy `gasPrice` 或 `maxFeePerGas` | ❌ 应为 `min(tipCap+baseFee, feeCap)` |
| TE buyGas/refund | `EthTxExecutor` 使用 `protocol::effectiveGasPrice()`（1559 时回退 `maxFeePerGas`，非 effective 公式） | ❌ |
| Coinbase tip / base fee | TE 路径无 tip 路由；base fee 未按 mainnet 销毁语义处理 | ❌ |
| EEST reference | `ExecuteViaEthAdapter` **已实现** `effectiveGasPriceForSettlement` + post-hoc settlement | ⚠️ 公式正确但未共享；`GASPRICE` 仍可能错 |

**已闭合（本 spec 不重复）：** W1–W4 precheck（`45c1c6c0f`）；7702 intrinsic / included-tx vmerr（ADR-015）。

### 1.1 预期收益（量化）

| 路径 | 本 PR 主要 delta | 说明 |
|------|------------------|------|
| **TE 生产**（`EthTxExecutor`） | **高** — buyGas/refund/coinbase/receipt 对齐 geth | 核心交付 |
| **Reference `GASPRICE`**（`ExecuteViaEth`） | **中** — 合约内 `GASPRICE`/`BASEFEE` 组合行为 | 不依赖 adapter 重复改 settlement |
| **EEST stateRoot** | **低～中** — adapter 结算 **已** 用 `min(tip+base,feeCap)` | 不应期望 537 fail 桶因 1559 单独大幅下降；收益在「读 gasprice 的 fixture」子集 |
| **EEST smoke** | **零退化** | 13/13 阻断 |

**验收锚点：** Step 6 必须跑 smoke；Step 6b 跑 **1559 GASPRICE probe manifest**（见 §7.4），记录 pass/fail 与 probe 前 baseline 对比，即使 delta 为 0 也写入 README footnote。

---

## 2. 范围决策（已确认）

| 维度 | 选择 |
|------|------|
| 架构方案 | **方案 1** — `eth/gas/Eip1559.h` 共享公式；orchestrator 各自调用；**不**在 `ExecuteViaEth` 内核做 balance settlement |
| 代码范围 | **B** — TE（`EthTxExecutor` + `EthTxInputBuilder` + `EthTransactionExecutorImpl`）+ reference（`ExecuteViaEth` + adapter 去重） |
| Framework | **不改** `protocol::effectiveGasPrice()`；eth TE 路径显式使用 `resolveEffectiveGasPrice`（见 §2.3 已知债务） |
| RevisionConfig | **不改** `eip1559` flag 消费（ADR-004 profile-only） |
| 1559 识别 | **唯一判据** `isEip1559GasCapsTx()`（§4.1）；禁止 `gasTipCap != 0` 等启发式 |
| OpStack 去重 | **defer** 至独立 follow-up PR；本 PR 不移动 `opstack::resolveEffectiveGasPrice` |
| Base fee 去向 | **L1 mainnet：销毁**（不 credit）；与 GST adapter 一致，**不同于** OpStack vault |
| Blob (type-3) | **不在范围** — blob gas fee 仍由 EIP-4844 路径处理；本 spec 仅 legacy/type-2/type-4 **execution gas** caps |

### 2.1 在范围内

- 新增 `bcos-evm/eth/eip/Eip1559.h`
- `ExecuteViaEth`：`gasPrice` normalization（含 **`eth_call`**）
- `EthTxInputBuilder` + `ExecuteContext::Data`：caps 与 `blockInfo`
- `EthTxExecutor::{buyGas,refundGas}` + **insufficient balance penalty**
- `EthTransactionExecutorImpl::settleGasUsedFromEvmResult`：**ADR-015 vmerr 路径**纳入 final `gasUsed`
- `ExecuteViaEthAdapter`：删 duplicate，用共享 helper；**结算逻辑不变**
- 单测 + ADR-016 + capability-matrix 行

### 2.2 不在范围内

- 块级 base fee 动态调整
- OpStack L1/operator/blob 路由；OpStack 同名函数去重
- `bcos-framework` / RPC / txpool 全面替换 `effectiveGasPrice()`
- 全量 EEST state full 537 fail closure
- BCOS `executeViaHost` 1559（feature-gated，后续复用 `Eip1559.h`）

### 2.3 已知债务（本 PR 不修复，须 grep 审计留痕）

| 调用方 | 用途 | 本 PR 处理 |
|--------|------|------------|
| `EthTxExecutor` / `EthTransactionExecutorImpl` | buyGas/refund/execute input | **改为** `resolveEffectiveGasPrice` |
| `TransactionExecutorImpl` / `FiscoTxExecutor` | 同上 | 不变（非 eth TE 路径） |
| `OpStackTxInputBuilder` | `gasFeeCap` 回退 | 不变 |
| `TxValidator` | 池内余额预检 | 不变；注释已知近似 |
| Receipt `effectiveGasPrice` 字段 | 元数据 | TE eth 路径写 **正确** effective |

Implementation plan Step 0：`rg effectiveGasPrice transaction-executor bcos-evm/eth` 确认无遗漏 eth 路径。

---

## 3. 架构决策

### 3.1 选定方案：共享 gas 公式 + 分层 orchestration

```
eth/gas/Eip1559.h     ← 纯函数 + isEip1559GasCapsTx
        ↑
   ┌────┴────┬──────────────────┐
   │         │                  │
EthTxExecutor  ExecuteViaEthAdapter
(buyGas/refund) (post-hoc GST settlement — 已有，仅去重)
        ↑
ExecuteViaEth ← gasPrice := effective（含 eth_call）；不写 State 余额
        ↓
executeMessage ← tx_gas_price + block_base_fee
```

### 3.2 ADR-005 边界

| 职责 | 层 |
|------|-----|
| 公式、`isEip1559GasCapsTx` | `eth/gas/Eip1559.h` |
| sender 预扣/退还、coinbase tip | orchestration |
| `GASPRICE` / `BASEFEE` 上下文 | `executeMessage` |
| Precheck fee cap | `ExecuteViaEthPreCheck`（已有） |

### 3.3 两套结算模型（等价性）

| | TE（geth 生产） | GST adapter（EEST） |
|--|----------------|---------------------|
| 时机 | buyGas 预扣 → execute → refundGas | execute 后一次性 post-hoc |
| sender 净支出 | `finalGasUsed × effective` | 同左 |
| coinbase | `finalGasUsed × tipPerGas` | 同左 |
| base fee | 销毁（不 mint） | 同左（sender 付 effective，miner 只收 tip） |

在 **`finalGasUsed` 定义一致**（含 7623 floor 与 ADR-015 vmerr peak）时，两模型 sender/coinbase 余额终态一致。§5.6 定义 `finalGasUsed`。

---

## 4. 共享模块 — `eth/gas/Eip1559.h`

### 4.1 API

```cpp
namespace bcos::evm::gas {

/// Typed tx with EIP-1559 fee market fields (type-2, type-4).
/// false for legacy (0x00) and type-1 (0x01) even if gasPrice != 0.
bool isEip1559GasCapsTx(uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx) noexcept;

bcos::u256 resolveEffectiveGasPrice(
    bcos::u256 const& gasTipCap,
    bcos::u256 const& gasFeeCap,
    bcos::u256 const& baseFee) noexcept;

bcos::u256 tipPerGas(
    bcos::u256 const& effectiveGasPrice,
    bcos::u256 const& baseFee) noexcept;

struct GasCaps {
    bcos::u256 gasTipCap;
    bcos::u256 gasFeeCap;
    bool isEip1559Caps;
};

GasCaps normalizeGasCaps(
    bcos::u256 gasPrice,
    bcos::u256 gasTipCap,
    bcos::u256 gasFeeCap,
    uint8_t web3TypedTxKind,
    bool hasExplicitFeeCapsFromTx) noexcept;

/// 1559: gasLimit * feeCap; legacy: gasLimit * gasPrice (via normalized caps).
bcos::u256 maxBalanceGasDebit(int64_t gasLimit, GasCaps const& caps) noexcept;

}  // namespace bcos::evm::gas
```

### 4.2 `isEip1559GasCapsTx` 规则（唯一判据）

```cpp
bool isEip1559GasCapsTx(uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx) noexcept
{
    if (web3TypedTxKind == 0x02 || web3TypedTxKind == 0x04)
        return true;
    return hasExplicitFeeCapsFromTx;  // GST: maxFee/maxPriority in JSON; TE: maxFeePerGas() non-empty
}
```

- **Type-1 (0x01)：** 恒 `false` — 使用 `gasPrice`，即使用 access list。
- **Legacy adapter 预填 `gasTipCap = gasPrice`：** 只要 `web3TypedTxKind == 0` 且 template 无 maxFee 字段 → **非 1559**。

### 4.3 公式（geth `state_transition.go`）

| 步骤 | geth | 本实现 |
|------|------|--------|
| Effective | `min(tipCap + baseFee, feeCap)` | `resolveEffectiveGasPrice` |
| Balance check (1559) | `gasLimit * feeCap + value` | `maxBalanceGasDebit` + value |
| Buy debit | `gasLimit * effective` | `EthTxExecutor::buyGas` |
| Refund sender | `(gasLimit - finalGasUsed) * effective` | `refundGas` |
| Tip to coinbase | `finalGasUsed * tipPerGas(effective, base)` | `refundGas` |
| Base fee | burned | 不 credit |

---

## 5. 组件修改规格

### 5.1 `ExecuteViaEth` — `GASPRICE` normalization

**时机：** `ethExecuteViaEthPreCheck` 之后、intrinsic gas 之前。

**`ExecuteViaEthInput` 新增字段：**

```cpp
bool hasExplicitFeeCaps{false};  // TE: from Data::m_hasExplicitFeeCaps; adapter: tmpl maxFee/maxPriority present
```

**Normalization 逻辑：**

```cpp
auto const caps = gas::normalizeGasCaps(
    input.gasPrice, input.gasTipCap, input.gasFeeCap,
    input.web3TypedTxKind, input.hasExplicitFeeCaps);
if (gas::isEip1559GasCapsTx(input.web3TypedTxKind, input.hasExplicitFeeCaps))
{
    input.gasPrice = gas::resolveEffectiveGasPrice(
        caps.gasTipCap, caps.gasFeeCap, input.blockInfo.baseFee);
}
// legacy: input.gasPrice unchanged
```

**`eth_call`（`m_call == true`）：** 同样执行 normalization（buyGas/refund 跳过，但合约内 `GASPRICE` 必须正确）。`EthTransactionExecutorImpl::executeViaEthTx` 在 call 路径仍传入 caps + baseFee。

**不修改 State 余额。**

### 5.2 `EthTxInputBuilder` + `EthTransactionExecutorImpl`

#### 5.2.1 执行时序（阻断）

当前 TE 顺序：

```
Prepare → Execute: buyGas → executeViaEthTx → settleGasUsed → refundGas
```

**`buyGas` 在 `executeViaEthTx` / `fillWeb3Fields` 之前。** 因此 caps、baseFee、blockInfo **不能**只在 `fillWeb3Fields` 里填充。

**Prepare 阶段（或 Execute 开头、buyGas 之前）必须完成：**

1. `m_blockInfo = eth_tx::buildEthBlockInfo(m_blockHeader, m_ledgerConfig)` → 缓存至 `Data::m_blockInfo`
2. 从 `m_transaction` 解析并缓存：
   - `m_gasTipCap` / `m_gasFeeCap`（`maxPriorityFeePerGas()` / `maxFeePerGas()` 非空时）
   - `m_hasExplicitFeeCaps = !tx.maxFeePerGas().empty()`
   - `m_web3TypedTxKind`（复用 `resolveWeb3AccessList` 或 `fillWeb3Fields` 同源逻辑）
3. Legacy：`m_gasPriceLegacy = u256(tx.gasPrice())`（供 normalize 与 execute input）

**`fillWeb3Fields`（executeViaEthTx 内，buyGas 之后）：** 仅把 **已缓存** 的 caps/kind 写入 `ExecuteViaEthInput`；不再调用 `protocol::effectiveGasPrice()`。

```cpp
input.gasTipCap = data.m_gasTipCap;
input.gasFeeCap = data.m_gasFeeCap;
input.hasExplicitFeeCaps = data.m_hasExplicitFeeCaps;
input.gasPrice = data.m_gasPriceLegacy;  // legacy gasPrice; ExecuteViaEth normalizes 1559
eth_tx::fillWeb3Fields(m_data->m_transaction.get(), input);  // accessList / 7702 auth only
```

**`ExecuteContext::Data` 新增：**

- `m_gasTipCap`, `m_gasFeeCap`, `m_hasExplicitFeeCaps`, `m_web3TypedTxKind`
- `m_gasPriceLegacy`（legacy tx 的 `gasPrice`；1559 时仍保留原始字段供 normalize）
- `m_effectiveGasPrice`（buyGas 计算后缓存）
- `m_blockInfo`（Prepare 缓存；refund coinbase/baseFee 用）
- `m_topLevelIncludedTxVmError`（`executeViaEthTx` 输出；供 `settleGasUsedFromEvmResult`）

**删除 / 替换：**

- Prepare 阶段 `tx.gasPrice = protocol::effectiveGasPrice(...)` → **删除**（warm 不需要 gasPrice）
- `executeViaEthTx` 中 `input.gasPrice = protocol::effectiveGasPrice(...)` → 改传 `m_gasPriceLegacy` + caps

### 5.3 `EthTxExecutor` — buyGas / refundGas / penalty

#### buyGas（成功路径）

1. `caps = normalizeGasCaps(m_gasPriceLegacy, m_gasTipCap, m_gasFeeCap, m_web3TypedTxKind, m_hasExplicitFeeCaps)`
2. `effective = resolveEffectiveGasPrice(caps.gasTipCap, caps.gasFeeCap, m_blockInfo.baseFee)` → 存 `m_effectiveGasPrice`
3. `balanceCheck = maxBalanceGasDebit(gasLimit, caps) + txValue`
4. 预扣 `gasLimit * effective`
5. `m_gasPriceStr = hex(effective)`

#### buyGas（insufficient balance — penalty 路径）

对齐 geth「无法 afford gas 但仍可能扣 intrinsic」：

1. 仍先算 `effective`（同上，**不用** `protocol::effectiveGasPrice()`）
2. `balanceCheck` 仍用 `maxBalanceGasDebit`（1559 用 **feeCap**，非 effective）
3. 若 `balance < balanceCheck`：
   - `penalty = min(balance, INTRINSIC_GAS * effective)`
   - 扣 `penalty`；`m_gasUsed = penalty / effective`（整数除法，与现逻辑同形）
   - receipt `effectiveGasPrice = effective`
   - **不** credit coinbase（tx 未 included）

#### refundGas

**前置：** `settleGasUsedFromEvmResult()` 已写入 **`m_gasUsed` = finalGasUsed**（§5.6）。

1. `gasRemaining = max(0, gasLimit - m_gasUsed)`
2. `sender += gasRemaining * m_effectiveGasPrice`
3. `tipCredit = m_gasUsed * tipPerGas(m_effectiveGasPrice, baseFee)`
4. 若 `tipCredit > 0`：
   ```cpp
   ledger::account::EVMAccount coinbaseAccount(
       data.m_rollbackableStorage, data.m_blockInfo.coinbase, /*create=*/false);
   auto bal = co_await coinbaseAccount.balance();
   co_await coinbaseAccount.setBalance(bal + tipCredit);
   ```
5. **不** credit base fee（burn）
6. **REVERT：** **不** rollback 至 `m_afterBuyGasSavepoint`；仍执行 1–4（gas 已预扣，sender 付 tip，与现 `EthTxExecutor` 一致）
7. **EVM hard fail**（`EVMC_*` 且非 `EVMC_REVERT`）：rollback 至 `m_afterBuyGasSavepoint` 后仍执行 1–4

#### makeReceipt

`m_gasPriceStr = hex(m_effectiveGasPrice)`。

### 5.4 `ExecuteViaEthAdapter` — 去重（行为不变）

1. 删除 `effectiveGasPriceForSettlement`。
2. 使用 `gas::isEip1559GasCapsTx(resolveWeb3TypedTxKind(tmpl), tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0)`（与 §4.2 **唯一判据** 一致；type-4 由 `resolveWeb3TypedTxKind` 返回 `0x04`）。
3. `effectiveGasPrice = gas::resolveEffectiveGasPrice(input.gasTipCap, input.gasFeeCap, testCase.env.baseFee)`（1559）；legacy 用 `tx.gasPrice`。
4. `applyGstTransactionSettlement` 逻辑 **不变**；`finalGasUsed` 仍来自现有 7623/ADR-015 分支（§5.6 右列）。
5. 设置 `input.hasExplicitFeeCaps = tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0`。
6. 依赖 `ExecuteViaEth` 内 normalization 修正 `GASPRICE`（adapter 不必重复设 `input.gasPrice`）。

### 5.5 `EthFixtureAdapter`

Legacy：`gasTipCap = gasFeeCap = gasPrice`，`web3TypedTxKind = 0`，`hasExplicitFeeCaps = false`。

### 5.6 `finalGasUsed` 与 ADR-015 / 7623 交叉（阻断）

**定义：** 用于 **fee settlement**（sender 扣款、coinbase tip、refund remaining）的 gas 单位数。

| 场景 | TE：`settleGasUsedFromEvmResult` | GST adapter（已有） |
|------|--------------------------------|---------------------|
| Prague+ 成功 + `eip7623` | `finalizeEthereumGasUsed(snapshot, floorToken)` | 同左 |
| Included top-level vmerr + `eip7623` | **`settleIncludedTopLevelTransactionGas(...)`**（**新增 TE 分支**，对齐 adapter） | 已有 |
| 成功、无 7623 | `gasLimit - evmGasLeft` | `TX_BASE_GAS + result.gasUsed`（**仅 GST**；TE **不加** `TX_BASE_GAS`） |
| Precheck reject | 无 settlement | 无 settlement |

> **TE vs GST 无 7623 行：** adapter 的 `TX_BASE_GAS + result.gasUsed` 是 GST legacy 近似（见 `ExecuteViaEthAdapter.cpp`）；TE 生产路径用 `gasLimit - evmGasLeft`，**禁止**把 adapter 公式抄进 TE。

**信号传递（TE vmerr 分支）：**

```cpp
// executeViaEthTx 返回后：
m_data->m_topLevelIncludedTxVmError = output.topLevelIncludedTxVmError;
// settleGasUsedFromEvmResult 读取 m_topLevelIncludedTxVmError
```

**1559 规则：**

- `tipCredit = finalGasUsed × tipPerGas(effective, baseFee)` — 始终用 **finalGasUsed**，不用 raw `gasLimit - gas_left`。
- Included-tx vmerr：**仍 included** → **仍** 执行 coinbase tip + sender refund（与 geth 一致；adapter 已在 `status==SUCCESS \|\| !stateDiff.empty()` 时 settlement）。
- `refundGas` 的 `gasRemaining = gasLimit - finalGasUsed`（不是 `gasLimit - (gasLimit - evmGasLeft)` 当 7623 bump 后）。

**TE 缺口（本 PR 必补）：** 当前 `EthTransactionExecutorImpl::settleGasUsedFromEvmResult` **无** `topLevelIncludedTxVmError` 分支；须与 adapter 对称增加，否则 1559 refund 与 ADR-015 冲突。

---

## 6. 数据流

（与 v1 相同；`finalGasUsed` 在 `settleGasUsedFromEvmResult` / adapter 7623 分支产出后再进入 refund/settlement。）

---

## 7. 测试策略

### 7.1 `EthEip1559GasTest.cpp`

| Case | 断言 |
|------|------|
| `tip_plus_base_below_fee_cap` | effective = tip + base |
| `tip_plus_base_above_fee_cap` | effective = feeCap |
| `legacy_type0_not_1559` | `isEip1559GasCapsTx(0, false)` → normalize 后 effective = gasPrice |
| `type1_not_1559` | `isEip1559GasCapsTx(0x01, false)` |
| `type2_zero_priority_fee` | tip=0, effective = min(base, feeCap) |
| `max_balance_debit_1559` | limit × feeCap |
| `max_balance_debit_legacy` | limit × gasPrice |

### 7.2 `EthTxExecutor1559Test.cpp`

| Case | 断言 |
|------|------|
| `buy_gas_debits_effective_times_limit` | sender Δ = −limit×effective |
| `refund_returns_unused_at_effective` | sender 加回 remaining×effective |
| `coinbase_receives_tip_only` | coinbase Δ = used×(eff−base) |
| `burn_identity` | **refundGas 完成后** sender 净支出 − coinbase 入账 = used×baseFee |
| `insufficient_balance_fee_cap_check` | balance < limit×feeCap → reject |
| `insufficient_balance_penalty_uses_effective` | penalty = min(bal, 21000×effective) |
| `7623_final_gas_used_drives_tip` | bump floor 后 tip 按 finalGasUsed |
| `included_vmerr_still_routes_tip` | ADR-015 vmerr + 1559 caps；coinbase > 0 |

### 7.3 Adapter

| Case | 断言 |
|------|------|
| `shared_formula_matches_legacy_local` | 删 duplicate 前后 effective/tip 一致 |

### 7.4 EEST 回归（Step 6 阻断 + 6b 探针）

| 套件 | 期望 |
|------|------|
| `ctest -L specs-tests-smoke` | 13/13 |
| `EthExecuteViaEthPreCheckTest` / `EthIncludedTxVmerrTest` | 不退化 |
| **`eth-eest-1559-gasprice-probe.json`**（新建） | 含读取 `GASPRICE` 的 state fixture（legacy + type-2）；记录 pass/fail；**允许 0 delta**，但必须跑并文档化 |

### 7.5 `eth_call`

| Case | 断言 |
|------|------|
| `call_path_gasprice_opcode` | `m_call=true` 时 executeViaEth 仍 normalize；直调或轻量 harness |

---

## 8. 文档与矩阵

| 产物 | 内容 |
|------|------|
| `bcos-evm/docs/adr/016-eth-eip1559-settlement.md` | 公式、burn、finalGasUsed、ADR-005/015 交叉 |
| `capability-matrix.md` | 新行 **EIP-1559 effective gas + tip settlement (ETH TE)** — `explicit` |
| `specs-tests/README.md` | 1559 probe baseline（含「0 delta 预期」说明） |

ADR-016 与 TE 代码 **同 PR**。

---

## 9. 实施顺序

| Step | 任务 | 验证 |
|------|------|------|
| 0 | grep 审计 `effectiveGasPrice` eth 路径 | 清单 §2.3 |
| 1 | `Eip1559.h` + `EthEip1559GasTest` | ctest |
| 2 | `ExecuteViaEth` normalization + eth_call | 单测 §7.5 |
| 3 | `fillWeb3Fields` + `Data` 字段；删 Prepare/executeViaEthTx 错误 helper | 编译 TE |
| 4 | `settleGasUsedFromEvmResult` **+ vmerr 分支** | `EthIncludedTxVmerrTest` 不退化 |
| 5 | `EthTxExecutor` buy/refund/penalty/coinbase | §7.2 |
| 6 | Adapter 去重 | smoke 13/13 |
| 6b | 1559 GASPRICE probe manifest + README | 记录 delta |
| 7 | ADR-016 + matrix | review |

**Defer：** OpStack `resolveEffectiveGasPrice` → `eth/gas` alias（独立 PR）。

---

## 10. 风险与缓解

| 风险 | 缓解 |
|------|------|
| TE vmerr 分支缺失导致 refund 错 | Step 4 阻断；§5.6 |
| Legacy 误判 1559 | 只用 `isEip1559GasCapsTx`；§7.1 type0/type1 cases |
| penalty 仍用 maxFee | §5.3 penalty 显式用 effective |
| coinbase 账户不存在 | `EVMAccount(..., create=false)`；测试 preState 含 coinbase 或零余额 |
| EEST 无可见 delta | §1.1 预期 + probe 文档化；PR 描述强调 TE |
| `protocol::effectiveGasPrice` 漂移 | §2.3 债务表 |

---

## 11. 验收标准

1. `resolveEffectiveGasPrice` / `tipPerGas` 与 OpStack 实现 **u256 一致**。
2. TE：1559 tx coinbase 收 tip；**refundGas 完成后** `sender_net - coinbase_net = finalGasUsed × baseFee`。
3. TE：insufficient balance 用 feeCap 检查、effective 罚扣。
4. `executeMessage`：`tx_gas_price == effective`（1559）；legacy 不变。
5. TE：`settleGasUsedFromEvmResult` 含 **included vmerr** 路径；1559 refund 用 **finalGasUsed**。
6. EEST smoke 13/13；1559 probe 已跑并记录（delta 可为 0）。
7. Adapter 无 local effective price 函数。
8. ADR-016 + matrix 同 PR。

---

## 12. 参考

- geth `core/state_transition.go` — `buyGas`, `refundGas`, `EffectiveGasTip`, `gasUsed` after floor
- `bcos-evm/opstack/OpStackTxExecutor.cpp`
- `bcos-evm/specs-tests/src/ExecuteViaEthAdapter.cpp`
- ADR-005, ADR-004, ADR-015

---

*Spec v1.2 — 二次审查修订完成；implementation plan: `docs/superpowers/plans/2026-06-21-eth-eip1559-settlement.md`。*
