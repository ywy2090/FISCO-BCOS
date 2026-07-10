# bcos-evm-ref：Jovian/Karst（tx+receipt）与 Isthmus P0 修复

**状态：** brainstorming 定案（方案 A）  
**日期：** 2026-07-10  
**范围模块：** 仅 `bcos-evm-ref/opstack/`（不改生产 `bcos-evm/opstack/`）  
**前置：** rev.8.2（`2026-07-09-bcos-evm-ref-rev8-opstack-foundation-design.md`）；Isthmus 执行薄层已落地；M6 零值差分护栏部分完成

---

## 0. 决策摘要

| ID | 决策 |
|----|------|
| D1 | 正确性范围 = 共识缺陷 + 护栏缺口（不含解冻 E-b t8n） |
| D2 | 只修/扩展 `bcos-evm-ref/opstack` |
| D3 | 本轮交付物先出本 spec；实施另开 plan |
| D4 | 清单组织 = P0/P1/P2 严重度（Isthmus 修复并入本文） |
| D5 | Jovian = **tx 执行 + receipt 派生**；不做块头 DA / extraData 校验 |
| D6 | Karst = **占位**（`karstConfig` 行为暂等同 Jovian） |
| D7 | Isthmus P0 与 Jovian/Karst **同一份 spec** |
| D8 | Receipt API = `OpTxReceipt` 包装 + 可单测的 `deriveOpReceiptMeta` |
| D9 | 实现路径 = **最小增量扩展现有 opstack/**（方案 A） |

---

## 1. 目标 / 非目标 / 成功标准

### 1.1 目标

1. **Jovian tx 执行**
   - Operator 公式：`gas × scalar × 100 + constant`（op-geth `newOperatorCostFuncOperatorFeeFix`）
   - Jovian precompile 输入限长表（bn256 / BLS；`0x100` P256 gas 仍为 3450）
   - `OpFeeParams` 解包 `da_footprint_gas_scalar`（L1Block slot8 bytes `[18,20)`）
   - `jovianConfig()`：`rev = EVMC_PRAGUE`，`has_operator_fee` / Jovian 公式 / da footprint 标志开启

2. **Receipt 派生**
   - 新增 `OpReceiptMeta` + `deriveOpReceiptMeta`
   - `opTransition` 返回 `OpTxReceipt { receipt, meta }`
   - Jovian+：填充 `da_footprint_gas_scalar` 与 `da_footprint`（语义对齐 op-geth receipt 字段 `BlobGasUsed`，**不是** L1 blob gas）

3. **Karst 占位**
   - `OpFork::Karst` + `karstConfig()`；执行与 receipt 行为暂等同 `jovianConfig()`（`fork` 枚举值不同）
   - 单测钉死可配置；上游出现独立语义后再分叉实现

4. **Isthmus P0（共识对齐）**
   - **P0-1**：`l1_cost == 0` 时不 `touch` / 不入账 `OP_L1_FEE_VAULT`（对齐 op-geth `L1CostFunc` 对空 `RollupCostData` 返回 `nil`）
   - **P0-2**：非 deposit 用户 tx 若 `signedTxEnvelope` 为空，`opValidate` 失败（禁止静默 `l1_cost = 0`）

### 1.2 非目标

- 块头 `BlobGasUsed` / `CalcDAFootprint` /「整块 DA ≤ gasLimit」校验
- Jovian `extraData` / `minBaseFee`（EIP-1559 参数编码）
- Isthmus `withdrawalsRoot`、OP `receiptsRoot` 等块头根
- E-b：ref t8n 全量 gate、生产切 `bcos-evm-ref` 内核
- 修改生产 `bcos-evm/opstack/`（可黑盒对照，不链接）
- Karst 独立执行语义（当前 op-geth 仅有配置钩子）

### 1.3 成功标准

- `jovianConfig` 下：operator 数值、precompile 限长、fee 解包单测全绿
- `deriveOpReceiptMeta`：Isthmus 有 L1/operator、无 da；Jovian 有 da = `estimatedDaSize × scalar`
- 所有 `opTransition` 调用方改为消费 `OpTxReceipt`
- P0-1 后零值差分可断言：fee=0 时 OP 侧 diff **无** L1 vault touch/delete
- P0-2：空 envelope → validate 失败
- README / `docs/vector-schema.md` 标注 Jovian；Karst 为占位；块验证仍非目标

---

## 2. 组件与 API

### 2.1 Fork 调度

```cpp
enum class OpFork {
    Ecotone, Fjord, Granite, Holocene, Isthmus, Jovian, Karst
};

struct OpForkConfig {
    OpFork fork;
    evmc_revision rev;                 // Isthmus/Jovian/Karst → EVMC_PRAGUE
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
    bool has_jovian_operator_formula;  // Jovian/Karst = true
    bool has_da_footprint;             // Jovian/Karst = true
};

const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;  // 行为标志同 jovian；fork = Karst
```

### 2.2 Fee 参数与代价

- `OpFeeParams` 增加 `uint16_t da_footprint_gas_scalar`（slot8 `[18,20)`）
- `unpackOpFeeParams` 始终解出该字段；Isthmus 路径可忽略
- `computeOperatorCost(fee, gas, cfg)`：
  - `has_jovian_operator_formula` → `gas × scalar × 100 + constant`
  - 否则 → Isthmus `gas × scalar / 1e6 + constant`
- 导出或内部提供 `estimatedDaSize(envelope)`，供 receipt：`da_footprint = size × scalar`
- **P0-2**：`opValidate` 对用户 tx 空 envelope → 失败
- **P0-1**：`opTransition` 仅当 `l1_cost != 0` 时 touch/入账 L1 vault

### 2.3 Precompile

- `jovianPrecompileOverrides()`：限长取自 op-geth `params` Jovian 常量；`0x100` gas=3450
- `jovianConfig().precompiles` / `karstConfig().precompiles` 指向该表（Karst 暂共用）

### 2.4 Receipt

```cpp
struct OpReceiptMeta {
    std::optional<intx::uint256> l1_fee;
    std::optional<intx::uint256> operator_fee;
    std::optional<uint32_t> operator_fee_scalar;
    std::optional<uint64_t> operator_fee_constant;
    std::optional<uint64_t> da_footprint_gas_scalar;  // Jovian+
    std::optional<uint64_t> da_footprint;             // receipt BlobGasUsed 语义
};

struct OpTxReceipt {
    evmone::state::TransactionReceipt receipt;
    OpReceiptMeta meta;
};

OpReceiptMeta deriveOpReceiptMeta(
    const OpForkConfig& cfg,
    const OpFeeParams& fee,
    evmc::bytes_view signedTxEnvelope,
    intx::uint256 l1_cost,
    intx::uint256 operator_fee_at_used,
    bool fill_operator_scalars) noexcept;

// 原返回 TransactionReceipt → 改为 OpTxReceipt（破坏性变更，测试同步改）
OpTxReceipt opTransition(...);
```

规则：

- 用户 tx：`l1_fee = l1_cost`；Isthmus+ 填 operator（及非零时的 scalar/constant）；Jovian+ 再填 da 字段
- Deposit：继续 `OpDepositReceipt`；**不**填 L1/operator/daFootprint（对齐 op-geth）
- EVM revert：仍产出 meta（按实际结算数字填充，与 geth derive 时机一致：费用字段不因 EVM 失败而省略）

### 2.5 文件落点

| 动作 | 路径 |
|------|------|
| 修改 | `OpForkSchedule.*`、`OpFeeParams.*`、`OpPrecompiles.*`、`RollupCost.*`、`OpValidate.*`、`OpTransition.*` |
| 新建 | `include/.../OpReceiptMeta.h`、`opstack/OpReceiptMeta.cpp`（或等价命名） |
| 测试 | `OpReceiptMetaTest`；扩展 ForkSchedule / Precompiles / FeeParams / Validate / Transition / ZeroDiff；更新 BlockHarness |
| 文档 | `bcos-evm-ref/README.md`、`bcos-evm-ref/docs/vector-schema.md` |

---

## 3. 数据流 / 错误处理 / 测试

### 3.1 用户 tx 数据流

```text
signedTxEnvelope + OpFeeParams
        │
        ▼
opValidate ── 拒 blob / 拒空 envelope(P0-2) / 余额 cap
        │      l1_cost、op_cost@gasLimit（公式随 cfg）
        ▼
opTransition
        │  buyGas → OpHost::call → refund + vaults
        │  L1 vault 仅 l1_cost≠0 (P0-1)
        │  operator @used + 退差额
        ▼
deriveOpReceiptMeta(...)
        ▼
OpTxReceipt { receipt, meta }
```

### 3.2 错误处理

| 情况 | 行为 |
|------|------|
| blob tx | `not_supported`（现状） |
| 空 envelope（用户 tx） | validate 失败（P0-2） |
| 余额不足 | `result_out_of_range`（现状） |
| EVM revert | `receipt.status ≠ success`；仍填 meta |
| system deposit | 抛块级错误（现状） |
| Karst | 无独立错误码；走 Jovian 路径 |

### 3.3 测试矩阵

| 套件 | 覆盖 |
|------|------|
| OpForkSchedule | jovian/karst 配置字段；Karst 行为标志 ≡ Jovian |
| RollupCost / operator | 同输入下 Isthmus vs Jovian 数值 |
| OpPrecompiles | Jovian 限长严于 Isthmus；超限失败 |
| OpFeeParams | slot8 解出 `da_footprint_gas_scalar` |
| OpReceiptMeta | Isthmus 无 da；Jovian da=size×scalar；deposit 不填 |
| OpValidate | 空 envelope 失败 |
| OpTransition / ZeroDiff | P0-1；返回 `OpTxReceipt` |
| OpBlockHarness | 可选 Jovian 冒烟断言 `meta.da_footprint` |

### 3.4 风险

| 风险 | 缓解 |
|------|------|
| `opTransition` 返回类型破坏性变更 | 同 PR 改全测试/harness |
| envelope ≠ geth `MarshalBinary` | P0-2 强制非空；向量用真实 typed envelope |
| 不做块级 DA 校验 | 显式非目标；E-b/块验证里程碑再补 |
| Karst 上游日后分叉 | 占位 + 单测；语义出现时再开 spec |

---

## 4. 与 rev.8 / M6 的关系

- 不解除 E-b park；不宣称 op-geth 块级等价
- M6 零值差分：P0-1 落地后收紧 L1 vault 断言；Jovian 路径不要求与 `eth::runTransaction` 整份 diff 等价（operator/da 为 OP 专有）
- 生产 `OpStackReceiptMeta` / `operatorCostJovian` 仅作黑盒对照，禁止 `#include <bcos-evm/...>`

---

## 5. 自检

- [x] 无 TBD 占位实现细节（块验证明确非目标）
- [x] §1 目标与 §2 API、§3 测试一致
- [x] 范围单一：ref opstack tx+receipt + Isthmus P0
- [x] `da_footprint` vs 块头 `BlobGasUsed` 语义已区分
- [x] Karst 占位 vs Jovian 实现边界明确
