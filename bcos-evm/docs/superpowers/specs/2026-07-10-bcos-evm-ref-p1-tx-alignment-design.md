# bcos-evm-ref：P1 交易执行对齐（7702 / 7623 / L1Block fee / vault stub / pre-Isthmus L1）

**状态：** brainstorming 定案（方案 A）  
**日期：** 2026-07-10  
**范围模块：** 仅 `bcos-evm-ref/opstack/`（不改生产 `bcos-evm/opstack/`）  
**前置：** rev.8.2；Jovian/Karst tx+receipt spec（`2026-07-10-bcos-evm-ref-jovian-karst-tx-receipt-design.md`，含 N-1/G-1/`OpTxReceipt`）

---

## 0. 决策摘要

| ID | 决策 |
|----|------|
| D1 | 覆盖上一轮清单 **全部 5 项 P1**（7702、7623 floor、L1Block 解 fee、vault stub、pre-Isthmus fork+历史 L1 公式） |
| D2 | 组织方式 = **单份 spec**（方案 A）；实现可按依赖拆 task，但仍属同一里程碑 |
| D3 | Pre-Isthmus = config **+** Ecotone vs Fjord L1 公式分叉（非仅占位 flags） |
| D4 | 7702 = **完整端到端矩阵**（含真实 ecrecover，非仅补恢复钩子） |
| D5 | L1Block fee = `loadOpFeeParams` helper **+** 从 state 读 fee 的 validate/transition 重载 |
| D6 | 7623 = 单测钉死公式 **+** ≥1 条真实向量回放 |
| D7 | Vault stub = 仅四个 fee vault 最小非空 code；**不改** L1Block / GasPriceOracle |
| D8 | Ecotone 首块 Bedrock L1 回退 = **非目标** |
| D9 | 不解除 E-b park；不宣称 op-geth 块级/生产等价 |

---

## 1. 目标 / 非目标 / 成功标准

### 1.1 目标

1. **EIP-7702 完整端到端**
   - `process_authorization_list`：`signer` 为空时用 `r/s/v` 做 EIP-7702 ecrecover；无效则 skip 该条 auth
   - 测试矩阵：成功授权、坏签名、nonce 不匹配、chain_id 不匹配、授权后委托调用 precompile（`EVMC_DELEGATED`）
   - 矩阵中 ≥3 条用例 **不得**预置 `auth.signer`，必须走恢复路径

2. **EIP-7623 floor 端到端**
   - 单测：`runDeposit` 与 `opTransition` 各 ≥1 条「执行消耗 < floor」的大 calldata，断言 `gas_used == max(执行, FloorDataGas)`（即 `min_gas_cost`）
   - ≥1 条真实边界向量回放（opt8n / 已入库金值；禁止手改期望凑绿）

3. **L1Block 现场解 fee**
   - `loadOpFeeParams(StateView)`：读 `OP_L1_BLOCK` slot1/3/7/8 → `unpackOpFeeParams`
   - 保留现有「调用方注入 `OpFeeParams`」API；新增从 state 读 fee 的薄重载（内部 `load` + 转调）
   - BlockHarness 改为使用 `loadOpFeeParams`；至少一条测试证明 FromState ≡ 注入 fee

4. **Vault 夹具保真**
   - `seedOpPredeploys`：对 `OP_BASE_FEE_VAULT` / `OP_L1_FEE_VAULT` / `OP_OPERATOR_FEE_VAULT` / `OP_SEQUENCER_FEE_VAULT` 写入最小非空 stub code（并更新 `code_hash`）
   - **不**修改 `OP_L1_BLOCK`、`OP_GAS_PRICE_ORACLE` 的 code（L1 attributes harness setter 自管）

5. **Pre-Isthmus fork + 历史 L1 公式**
   - 新增 `ecotoneConfig()` / `fjordConfig()` / `graniteConfig()` / `holoceneConfig()`，单测钉死 `fork` / `rev` / flags / precompiles
   - `computeL1Cost(..., cfg)`：Ecotone 走 calldataGas 公式；Fjord+ 走现有 FastLZ 公式
   - Granite/Holocene 行为标志暂 ≡ Fjord（仅枚举不同），同 Karst≡Jovian 模式

### 1.2 非目标

- E-b：ref t8n 全量 gate、生产切 `bcos-evm-ref` 内核
- 块头 DA / `extraData` / `minBaseFee` / `withdrawalsRoot` / OP `receiptsRoot`
- 修改生产 `bcos-evm/opstack/`（含 FINDING-1 deposit floor 修复——属生产轨）
- Ecotone **首块** Bedrock L1 cost 回退边角（op-geth `firstEcotoneBlock`）
- 真链 vault 完整 runtime 字节码 / 字节码哈希钉死
- Karst 独立执行语义；Bedrock/Regolith 更早 L1 公式
- M6 upstream diff 脚本（仍属 E-b / M6 收尾）
- **pre-Isthmus precompile 集合保真**（Fjord 起 P256@0x100、Granite bn256 限长、pre-Isthmus 无 BLS）——本轮 pre-Isthmus config 一律 `precompiles = nullptr`，不逐 fork 建表；**严禁**复用 `isthmusPrecompileOverrides()` 以免植入假激活。运行带 precompile 的 tx 于 pre-Isthmus fork 非本轮验收范围

### 1.3 成功标准

- 7702：无预置 signer 的真实签名用例全绿；失败类不写 delegation、不错误 bump authority nonce
- 7623：单测数字正确 + ≥1 条真实向量回放绿
- `loadOpFeeParams` 与手读四槽一致；`*FromState` 与注入 fee 路径结果一致（同块同槽）
- 零值差分（fee=0）下：L1 vault **不再**因空账户进入 `deleted_accounts`（或等价断言收紧）
- 同 envelope 下 Ecotone vs Fjord `computeL1Cost` 数值不同且符合各自公式；各 pre-Isthmus config 字段钉死
- 现有 opstack 回归全绿；N-1/G-1/`OpTxReceipt` 行为不回退
- README 标注本里程碑；仍不宣称 op-geth 块级/生产等价

---

## 2. 组件与 API

### 2.1 Fork 调度

```cpp
struct OpForkConfig {
    OpFork fork;
    evmc_revision rev;
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
    bool has_jovian_operator_formula;
    bool has_da_footprint;
    bool has_ecotone_l1_formula;  // true → Ecotone calldataGas 路径；false → Fjord+ FastLZ
};

const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
// 已有：isthmusConfig / jovianConfig / karstConfig
```

| Config | `has_ecotone_l1_formula` | `has_operator_fee` | `has_jovian_operator_formula` | `has_da_footprint` | `rev`（约定） |
|--------|--------------------------|--------------------|-------------------------------|--------------------|---------------|
| Ecotone | true | false | false | false | Cancun 系（`EVMC_CANCUN`） |
| Fjord | false | false | false | false | Cancun 系 |
| Granite | false | false | false | false | ≡ Fjord |
| Holocene | false | false | false | false | ≡ Fjord |
| Isthmus | false | true | false | false | `EVMC_PRAGUE`（现状） |
| Jovian/Karst | false | true | true | true | `EVMC_PRAGUE`（现状） |

- Ecotone–Holocene `precompiles = nullptr`（即该 `rev` 的 evmone base 表，无 override）。
  **理由（重要）**：当前 `isthmusPrecompileOverrides()` 含 `0x100`(P256, 3450)、`0x0c/0e/0f`(Prague BLS 限长)、`0x08`(bn256 限长)。而 op-geth 中这些激活点各不相同：P256 自 **Fjord**(RIP-7212)、bn256 pairing 限长自 **Granite**、BLS 自 **Isthmus**(Prague)。若给 pre-Isthmus 复用 Isthmus 表，会把「Ecotone 有 P256/BLS」等**假激活**被单测固化成"正确"。指向 `nullptr` 是**安全欠近似**（Fjord/Granite 会少 P256/bn256 限长，但绝不假激活），并由下方非目标声明覆盖。
- `isthmusConfig()` 等现有 config 必须显式设 `has_ecotone_l1_formula = false`

### 2.2 `RollupCost` — fork 感知 L1

```cpp
intx::uint256 computeL1Cost(const OpFeeParams& params, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg) noexcept;
```

- **Fjord+**（`!has_ecotone_l1_formula`）：保持现实现  
  `estimatedDaSizeScaled(flz) * (l1BaseFee*baseScalar*16 + blobBaseFee*blobScalar) / 1e12`
- **Ecotone**（`has_ecotone_l1_formula`）：对齐 op-geth `newL1CostFuncEcotone`  
  `calldataGas * (l1BaseFee*16*baseScalar + blobBaseFee*blobScalar) / 16e6`  
  其中 `calldataGas = bedrockCalldataGasUsed(envelope)`
  - **需新增计数 helper**（ref 现仅有 `flzCompressLen`，无零/非零字节计数）：  
    `uint64_t bedrockCalldataGasUsed(evmc::bytes_view env)` = `zeroes*4 + nonZeroes*16`  
    （`TxDataZeroGas=4`、`TxDataNonZeroGasEIP2028=16`）
  - **钉死无 `+68` overhead**：op-geth `rollup_cost.go:662 bedrockCalldataGasUsed` 的 Ecotone 分支不含 pre-Regolith 的 `+68`；单测须有一条断言 `+68` 未被计入
- 空 envelope → `0`（与现状一致；用户 tx 仍受 G-1 约束）
- **破坏性**：所有 `computeL1Cost` 调用点（含 `opValidate`）传入 `cfg`
- **非目标**：首块 Bedrock 回退

### 2.3 `loadOpFeeParams` + 重载

```cpp
OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept;
// 读 OP_L1_BLOCK 的 slot 1/3/7/8（缺槽视为零字）→ unpackOpFeeParams
```

重载形态（薄封装，避免复制结算逻辑）：

```cpp
// 保留：
opValidate(..., const OpFeeParams& fee, ...);
opTransition(..., const OpFeeParams& fee, ..., envelope);

// 新增：
opValidateFromState(..., cfg, blockGasLeft);           // 内部 loadOpFeeParams(view)
opTransitionFromState(..., cfg, vm, props, chainId, envelope);
// FromState 的 transition：再次 load（同块一致）以单一真相来源
```

**配对约束（必须写入头文件注释 + 单测断言）**：`*FromState` 对必须**成对使用**。
`props` 来自 `opValidateFromState`（其 `op_cost` 按 `gas_limit`、仅用于余额 cap），
`opTransitionFromState` 再 load 一次 fee 用于结算/`deriveOpReceiptMeta`。
**禁止跨用**（注入 fee 的 `opValidate` + `opTransitionFromState`，或反之）——
否则两侧 fee 可能来自不同源。同块同状态下二次 load 必然一致，故安全。

BlockHarness：attributes deposit 之后改用 `loadOpFeeParams(ts)`，删除手写四槽拼接（或保留为对照断言一次）。

### 2.4 Vault stub

```cpp
void seedOpPredeploys(evmone::test::TestState& state);
// 对四个 vault：插入账户 + code = {0x00}（或等价单字节非空）+ 更新 code_hash
// 仍预填 OP_L1_BLOCK / OP_GAS_PRICE_ORACLE 账户存在性，但不写其 code
```

更新头文件注释（删除「真实 bytecode 延后」中针对 vault 的过时表述）。

### 2.5 EIP-7623 验收

- 执行路径已使用 `props.min_gas_cost` / `p.min_gas_cost`；本轮 **不改公式实现**，补验收
- 单测构造：calldata 使 `FloorDataGas >` 实际 EVM 消耗（简单转账/空执行 + 大 data）
- 向量：从 `bcos-evm/test/opstack/t8n/vectors/` 或 opt8n 产物移植 ≥1 条到 `bcos-evm-ref/test/opstack/fixtures/`（或等价路径）
- **实现分级（避免过度投入 replayer）**：ref 侧当前**无** t8n 回放器（那属被禁 include 的 `bcos-evm/test/opstack` M-T 线）。优先做**硬编码金值 fixture**——把该向量的输入（tx/env/fee/cfg）与期望 `gas_used` 写成 C++ 常量，直接喂 `runDeposit`/`opTransition` 断言。仅当后续需要时才升级为 JSON 最小 replayer。**不必**上完整 t8n gate
- 期望值来源：opt8n 真跑或已审查金值；测试注释注明来源、向量原始路径、抓取时间
- **注意**：本项的核心正确性由 §2.5 单测（精确钉公式）保证；金值 fixture 是补充边界证据，非公式唯一来源

### 2.6 EIP-7702 ecrecover

在 `process_authorization_list` 中：

1. 现有校验保留：`chain_id`、`nonce != NonceMax`、`v∈{0,1}`、`s ≤ N/2`
2. 若 `!auth.signer.has_value()`：对 EIP-7702 签名消息做 `ecrecover`（`evmone_precompiles` / secp256k1）；失败 → `continue`
3. 若已预置 `signer`：可跳过恢复（测试捷径），但端到端矩阵不得依赖此捷径凑数
4. 其后逻辑（accessed、code 空或已委托、nonce 匹配、写 `0xef0100||addr` 或清委托、nonce++、refund）保持与现照抄面一致

签名消息：`keccak256(MAGIC || rlp([chain_id, address, nonce]))`，与 EIP-7702 / evmone 母本一致。

### 2.7 文件落点

| 动作 | 路径 |
|------|------|
| 修改 | `OpForkSchedule.*`、`RollupCost.*`、`OpFeeParams.*`、`OpValidate.*`、`OpTransition.*`、`OpPredeploys.*` |
| 测试 | 扩展 ForkSchedule / RollupCost / FeeParams / Predeploys / ZeroDiff / BlockHarness；新建 `OpFloorGasTest`、`Op7702Test`；≥1 向量 fixture |
| 文档 | `bcos-evm-ref/README.md`；必要时 `docs/vector-schema.md` |

---

## 3. 数据流 / 错误处理 / 测试

### 3.1 用户 tx（FromState）

```text
StateView (L1Block slots)
        │
        ▼
loadOpFeeParams(view) → OpFeeParams
        │
        ▼
opValidate[FromState]
        │  拒 blob / 拒空 envelope(G-1) / 余额 cap
        │  computeL1Cost(fee, envelope, cfg)   ← Ecotone vs Fjord+
        │  computeOperatorCost(fee, gasLimit, cfg)
        ▼
OpTxProperties
        │
        ▼
opTransition[FromState]
        │  buyGas → process_authorization_list(ecrecover) → OpHost::call
        │  gas_used = max(执行, min_gas_cost)
        │  vaults（N-1 无条件 L1 touch）→ deriveOpReceiptMeta
        ▼
OpTxReceipt
```

Deposit：不经 L1/operator buyGas；floor 仍经 `min_gas_cost`；`is_system_tx` 仍抛块级错误。

### 3.2 错误处理

| 情况 | 行为 |
|------|------|
| blob tx | `not_supported` |
| 空 envelope（用户 tx） | `invalid_argument`（G-1） |
| 余额不足 | `result_out_of_range` |
| 7702 单条 auth 校验/恢复失败 | skip 该条；整笔 tx 不因此失败 |
| L1Block 槽缺失 | `loadOpFeeParams` 按零字；可能得到 0 fee（G-1 仍要求非空 envelope） |
| Ecotone 首块 Bedrock 回退 | 不实现 |
| `is_system_tx` deposit | `std::runtime_error` |

### 3.3 测试矩阵

| 套件 | 覆盖 |
|------|------|
| OpForkSchedule | ecotone/fjord/granite/holocene；Granite/Holocene ≡ Fjord；Isthmus+ `has_ecotone_l1_formula=false`；**pre-Isthmus `precompiles==nullptr`** |
| RollupCost | 同 envelope：Ecotone ≠ Fjord 数值；两公式钉死；**`bedrockCalldataGasUsed` 无 `+68` 断言** |
| OpFeeParams / Harness | `loadOpFeeParams` ≡ 手读；FromState ≡ 注入 |
| OpPredeploys / ZeroDiff | vault 非空 code；fee=0 时 L1 vault 不在 `deleted_accounts` |
| OpFloorGas | deposit + user floor 抬升；≥1 真实向量 |
| Op7702 | §1.1 / §2.6 五类；≥3 条无预置 signer |
| 回归 | 全量 `bcos-evm-ref-opstack-tests` 绿 |

### 3.4 风险

| 风险 | 缓解 |
|------|------|
| Ecotone / Fjord 公式混淆 | 单测同输入双路径；注释引用 op-geth 符号名 |
| 7702 哈希与上游不一致 | 对照 evmone 母本 + 已知向量 |
| 向量期望被「凑绿」 | 纪律：期望只许来自 opt8n/金值；来源写入测试注释 |
| `computeL1Cost` 加 `cfg` 破坏性 | 同 PR 改全调用点 |
| vault stub 破坏 L1Block setter | 明确不写 L1Block code |

---

## 4. 与 rev.8 / Jovian spec / E-b 的关系

- 不解除 E-b park；正确性证据仍止于单测 + harness +（本轮）floor/7702 向量，**不构成** op-geth 全量等价
- 继承 Jovian/Karst spec 的 N-1（L1 vault 无条件 touch）与 G-1（空 envelope）
- 生产 FINDING-1 不在本范围；ref 侧用 7623 向量独立钉死同类语义
- 禁止 `#include <bcos-evm/...>`
- **E-b 接线安全护栏（终审记录）**：`process_authorization_list` 保留「若 `auth.signer` 已预置则信任、跳过 ecrecover」的捷径（仅为单测注入便利，signer 是 evmone test-state 字段，非从不可信 envelope 解码）。E-b 生产接线**绝不可**用不可信输入填充 `auth.signer`——生产路径必须始终走 `recoverAuthority` 真实恢复，否则等同放行伪造授权。

---

## 5. 自检

- [x] 无 TBD 占位实现细节（Bedrock 首块回退已标非目标）
- [x] §1 目标与 §2 API、§3 测试一致
- [x] 范围单一：ref opstack P1 五线；E-b/块头/生产轨排除
- [x] Ecotone vs Fjord L1、7702 ecrecover、load/重载、vault 四地址、floor 双验收均有明确成功标准
- [x] 与 N-1/G-1/`OpTxReceipt` 无冲突
- [x] pre-Isthmus `precompiles=nullptr`（安全欠近似），假激活风险已列非目标
- [x] Ecotone `bedrockCalldataGasUsed` 为新增计数 helper、钉死无 `+68`
- [x] 7623 向量分级（金值 fixture 优先，replayer 后置），公式正确性由单测保证
- [x] `*FromState` 成对使用约束已明确
