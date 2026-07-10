# bcos-evm-ref：Jovian/Karst（tx+receipt）与 Isthmus P0 修复

**状态：** brainstorming 定案（方案 A）+ 二轮 op-geth 核对修订  
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
| D10 | **（二轮修订）** 撤销原 P0-1「`l1_cost==0` 不 touch L1 vault」——经核对 op-geth `AddBalance(vault,0)` 对空账户仍 `touch`，ref 现「无条件 touch」已与之一致；零值差分在断言层排除 vault（M6 已落地）。原 P0-2 降级为护栏 G-1 |
| D11 | **（二轮修订）** receipt 补齐 op-geth `deriveOPStackFields` 的 L1 直通字段（`L1GasPrice`/`L1BlobBaseFee`/两个 L1 scalar）；`L1GasUsed`（Fjord+ 恒 0）与 `FeeScalar`（pre-Ecotone 遗留）列为非目标；`operator_fee`（实收值）标注为 FISCO 扩展（op-geth receipt 无此字段） |

---

## 1. 目标 / 非目标 / 成功标准

### 1.1 目标

1. **Jovian tx 执行**
   - Operator 公式：`gas × scalar × 100 + constant`（op-geth `newOperatorCostFuncOperatorFeeFix`）
   - Jovian precompile 输入限长表（bn256 / BLS；`0x100` P256 gas 仍为 3450）
   - `OpFeeParams` 解包 `da_footprint_gas_scalar`（L1Block slot8 bytes `[18,20)`）
   - `jovianConfig()`：`rev = EVMC_PRAGUE`，`has_operator_fee` / Jovian 公式 / da footprint 标志开启

2. **Receipt 派生**（对齐 op-geth `deriveOPStackFields`）
   - 新增 `OpReceiptMeta` + `deriveOpReceiptMeta`
   - `opTransition` 返回 `OpTxReceipt { receipt, meta }`
   - L1 直通字段：`l1_gas_price`(=l1BaseFee)、`l1_blob_base_fee`、`l1_base_fee_scalar`、`l1_blob_base_fee_scalar`、`l1_fee`(=l1_cost)
   - Isthmus+：`operator_fee_scalar`/`operator_fee_constant` 仅在（scalar≠0 ∨ constant≠0）时填（对齐 op-geth 守卫）；`operator_fee`（实收值）为 **FISCO 扩展**（op-geth receipt 无此字段）
   - Jovian+：填充 `da_footprint_gas_scalar` 与 `da_footprint`（语义对齐 op-geth receipt 字段 `BlobGasUsed`，**不是** L1 blob gas）

3. **Karst 占位**
   - `OpFork::Karst` + `karstConfig()`；执行与 receipt 行为暂等同 `jovianConfig()`（`fork` 枚举值不同）
   - 单测钉死可配置；上游出现独立语义后再分叉实现

4. **Isthmus 护栏与澄清（二轮修订）**
   - **G-1（护栏，原 P0-2）**：非 deposit 用户 tx 若 `signedTxEnvelope` 为空，`opValidate` 失败（禁止静默 `l1_cost = 0`；op-geth 中 envelope 由 `MarshalBinary` 得到，绝不为空）
   - **N-1（澄清，撤销原 P0-1）**：**不改** `opTransition` 的 L1 vault 结算。核对 op-geth `state_transition.go` + `stateObject.AddBalance`：普通 tx 恒调 `AddBalance(L1FeeVault, l1Cost)`，即使 `l1Cost==0` 且账户为空也会 `touch`（随后 EIP-161 剪除）。ref 现「无条件 touch」已与之一致；`L1CostFunc` 返回 `nil` 仅指 deposit/RPC 的**空 `RollupCostData`**，与「算出的 cost 恰为 0」无关。零值差分中 L1 vault 出现在 `deleted_accounts` 是 **vault 被建成空账户的测试夹具产物**，已在 M6 断言层用 `nonVaultDeleted` 排除
   - **N-1 可选保真项**：`seedOpPredeploys` 给 vault 预置 code（使其非空，与真链一致 → `AddBalance(0)` 为 no-op、不剪除）；非阻塞，列为跟进

### 1.2 非目标

- 块头 `BlobGasUsed` / `CalcDAFootprint` /「整块 DA ≤ gasLimit」校验
- Jovian `extraData` / `minBaseFee`（EIP-1559 参数编码）
- Isthmus `withdrawalsRoot`、OP `receiptsRoot` 等块头根
- E-b：ref t8n 全量 gate、生产切 `bcos-evm-ref` 内核
- 修改生产 `bcos-evm/opstack/`（可黑盒对照，不链接）
- Karst 独立执行语义（当前 op-geth 仅有配置钩子）
- receipt 字段 `L1GasUsed`（Fjord+ 恒 0）与 `FeeScalar`（pre-Ecotone 遗留）
- 改动 `opTransition` 的 vault 结算（见 N-1；ref 现状已与 op-geth 一致）

### 1.3 成功标准

- `jovianConfig` 下：operator 数值、precompile 限长、fee 解包单测全绿
- `deriveOpReceiptMeta`：Isthmus 有 L1 直通字段 + operator；无 da；Jovian 有 da = `estimatedDaSize × scalar`
- 所有 `opTransition` 调用方改为消费 `OpTxReceipt`
- G-1：空 envelope → validate 失败
- N-1：`opTransition` L1 vault 结算逻辑**不变**；零值差分断言层已排除 vault（M6 现状保持绿）
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
- **G-1**：`opValidate` 对用户 tx 空 envelope → 失败
- **N-1**：`opTransition` L1 vault 结算**保持无条件 touch/入账**（与 op-geth 一致），本轮不改

### 2.3 Precompile

- `jovianPrecompileOverrides()`：限长取自 op-geth `params` Jovian 常量；`0x100` gas=3450
- `jovianConfig().precompiles` / `karstConfig().precompiles` 指向该表（Karst 暂共用）

### 2.4 Receipt

```cpp
struct OpReceiptMeta {
    // L1 直通（op-geth: L1GasPrice / L1BlobBaseFee / L1BaseFeeScalar / L1BlobBaseFeeScalar / L1Fee）
    std::optional<intx::uint256> l1_gas_price;          // = fee.l1_base_fee
    std::optional<intx::uint256> l1_blob_base_fee;      // = fee.blob_base_fee
    std::optional<uint32_t> l1_base_fee_scalar;
    std::optional<uint32_t> l1_blob_base_fee_scalar;
    std::optional<intx::uint256> l1_fee;                // = l1_cost
    // operator（Isthmus+）
    std::optional<intx::uint256> operator_fee;          // FISCO 扩展：实收值（op-geth receipt 无此字段）
    std::optional<uint32_t> operator_fee_scalar;        // 仅 (scalar≠0 ∨ constant≠0) 时填
    std::optional<uint64_t> operator_fee_constant;
    // DA footprint（Jovian+；op-geth receipt BlobGasUsed 语义）
    std::optional<uint64_t> da_footprint_gas_scalar;
    std::optional<uint64_t> da_footprint;
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

- 用户 tx：填 L1 直通字段（`l1_gas_price`/`l1_blob_base_fee`/两个 scalar/`l1_fee=l1_cost`）；`has_operator_fee` 填 `operator_fee`，且 (scalar≠0 ∨ constant≠0) 时填 scalar/constant；`has_da_footprint` 填 da 字段
- **非目标字段**：`L1GasUsed`（op-geth Fjord+ 恒 0）、`FeeScalar`（pre-Ecotone 遗留浮点标量）——本轮不引入
- Deposit：继续 `OpDepositReceipt`；**不**填 L1/operator/daFootprint（对齐 op-geth `deriveOPStackFields` 跳过 deposit）
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
opValidate ── 拒 blob / 拒空 envelope(G-1) / 余额 cap
        │      l1_cost、op_cost@gasLimit（公式随 cfg）
        ▼
opTransition
        │  buyGas → OpHost::call → refund + vaults
        │  L1 vault 无条件入账（N-1；与 op-geth 一致，本轮不改）
        │  operator @used + 退差额
        ▼
deriveOpReceiptMeta(...)
        ▼
OpTxReceipt { receipt, meta }
```

### 3.2 错误处理

| 情况 | 行为 |
|------|------|
| blob tx | `not_supported`（现状；blob 检查在 envelope 检查**之前**） |
| 空 envelope（用户 tx） | validate 失败（G-1，`invalid_argument`） |
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
| OpReceiptMeta | L1 直通字段；operator 守卫；Isthmus 无 da；Jovian da=size×scalar；deposit 不填 |
| OpValidate | 空 envelope 失败（G-1） |
| OpTransition / ZeroDiff | 返回 `OpTxReceipt`；ZeroDiff 保持 M6 断言（vault 排除） |
| OpBlockHarness | 可选 Jovian 冒烟断言 `meta.da_footprint` |

### 3.4 风险

| 风险 | 缓解 |
|------|------|
| `opTransition` 返回类型破坏性变更 | 同 PR 改全测试/harness |
| envelope ≠ geth `MarshalBinary` | G-1 强制非空；向量用真实 typed envelope |
| 不做块级 DA 校验 | 显式非目标；E-b/块验证里程碑再补 |
| Karst 上游日后分叉 | 占位 + 单测；语义出现时再开 spec |

---

## 4. 与 rev.8 / M6 的关系

- 不解除 E-b park；不宣称 op-geth 块级等价
- M6 零值差分：断言层维持现状（`nonVaultDeleted` 排除 vault）；**不因本轮改动**收紧或改动 L1 vault 断言（见 N-1）；Jovian 路径不要求与 `eth::runTransaction` 整份 diff 等价（operator/da 为 OP 专有）
- 生产 `OpStackReceiptMeta` / `operatorCostJovian` 仅作黑盒对照，禁止 `#include <bcos-evm/...>`

---

## 5. 自检

- [x] 无 TBD 占位实现细节（块验证明确非目标）
- [x] §1 目标与 §2 API、§3 测试一致
- [x] 范围单一：ref opstack tx+receipt + Isthmus 护栏 G-1
- [x] `da_footprint` vs 块头 `BlobGasUsed` 语义已区分
- [x] Karst 占位 vs Jovian 实现边界明确
- [x] 二轮修订：撤销 P0-1（N-1 澄清）；receipt L1 直通字段补齐；`L1GasUsed`/`FeeScalar` 非目标；`operator_fee` 标注 FISCO 扩展
