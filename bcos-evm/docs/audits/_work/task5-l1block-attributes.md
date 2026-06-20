# Task 5 — L1Block Predeploy + L1 Attributes Deposit 审计笔记（增补 S2）

**日期：** 2026-06-20  
**范围：** matrix 行 `chain precompile routing (L1Block)`（deviation）；增补 S2 `L1 attributes system deposit`  
**参考：** op-geth `e8800cffe` — `params/protocol_params.go`, `core/types/rollup_cost.go`；optimism `L1Block.sol` @ contracts-bedrock  
**Specs：** optimism/specs `689a96f` — `protocol/isthmus/l1-attributes.md`, `protocol/isthmus/predeploys.md`, `experimental/contracts/L2/l1-block.md`

---

## Step 1: Predeploy 地址与 storage slot 对照

### 地址（`OpStackConstants.h` vs op-geth）

| 常量 | FB | op-geth | 一致 |
|------|----|---------|------|
| L1Block predeploy | `0x4200…0015` | `rollup_cost.go:L1BlockAddr` | ✅ |
| Depositor account | `0xDeaD…0001` | `Constants.DEPOSITOR_ACCOUNT` / `receiptreference.go:systemAddress` | ✅ |
| L1 fee recipient | `0x4200…001A` | `OptimismL1FeeRecipient` | ✅ |
| Operator fee recipient | `0x4200…001B` | `OptimismOperatorFeeRecipient` | ✅ |
| Base fee recipient | `0x4200…0019` | `OptimismBaseFeeRecipient` | ✅ |

GasPriceOracle predeploy（`0x4200…000F`）**未**在 FB TE 路径实现；op-geth L1/operator fee 亦直接 `GetState(L1BlockAddr, slot)`，不依赖 GPO 合约调用 ✅（TE 路径一致）。

### Storage slot（fee 相关 — TE 消费面）

| Slot | FB `OpStackConstants.h` | op-geth `rollup_cost.go` | 用途 |
|------|---------------------------|--------------------------|------|
| 1 | `L1_BASE_FEE_SLOT` | `L1BaseFeeSlot` | L1 base fee |
| 3 | `L1_FEE_SCALARS_SLOT` | `L1FeeScalarsSlot` | baseFeeScalar @ bytes [16:20), blobBaseFeeScalar @ [20:24) |
| 7 | `L1_BLOB_BASE_FEE_SLOT` | `L1BlobBaseFeeSlot` | blob base fee |
| 8 | `OPERATOR_FEE_PARAMS_SLOT` | `OperatorFeeParamsSlot` | operatorFeeScalar @ [20:24), operatorFeeConstant @ [24:32) |

Scalar section offset：`kScalarSectionStart = 32 - 12 - 4 = 16`（`OpStackFee.cpp:10`）与 op-geth `scalarSectionStart` / `BaseFeeScalarSlotOffset=12` 一致 ✅

`ISTHMUS_L1_ATTRIBUTES_LEN = 176` 与 op-geth `IsthmusL1AttributesLen` 一致 ✅

---

## Step 2: `parseIsthmusL1Attributes` — 176 字节 calldata 布局

### Spec MUST（`isthmus/l1-attributes.md`）

| Offset | 字段 | 类型 | FB `L1BlockStorage.cpp` |
|--------|------|------|-------------------------|
| 0–3 | selector `setL1BlockValuesIsthmus()` | — | 由 `L1BlockPredeploy` 读取，不参与 parse |
| 4–7 | baseFeeScalar | uint32 | `readU32(4)` ✅ |
| 8–11 | blobBaseFeeScalar | uint32 | `readU32(8)` ✅ |
| 12–19 | sequenceNumber | uint64 | `readU64(12)` ✅ 解析但未写入 state |
| 20–27 | l1BlockTimestamp | uint64 | `readU64(20)` ✅ 解析但未写入 |
| 28–35 | l1BlockNumber | uint64 | `readU64(28)` ✅ 解析但未写入 |
| 36–67 | basefee | uint256 | `readBytes32(36)` → slot 1 ✅ |
| 68–99 | blobBaseFee | uint256 | `readBytes32(68)` → slot 7 ✅ |
| 100–131 | l1BlockHash | bytes32 | `readBytes32(100)` ✅ 解析但未写入 |
| 132–163 | batcherHash | bytes32 | `readBytes32(132)` ✅ 解析但未写入 |
| 164–167 | operatorFeeScalar | uint32 | `readU32(164)` → slot 8 ✅ |
| 168–175 | operatorFeeConstant | uint64 | `readU64(168)` → slot 8 ✅ |

Selector 金标准：`keccak256("setL1BlockValuesIsthmus()")[:4]` = **`0x098999be`** ✅

### Fixture 对照（`isthmus_l1_attributes.bin`）

```
098999be 11223344 55667788 0102030405060708 1112131415161718 2122232425262728
…000…0123456789abcdef…  …0fedcba987654321…  hash@100  batcherHash@132  a1b2c3d4 0102030405060708
```

`L1BlockPredeployTest::setter_unpacks_isthmus_fixture_into_slots` 对 slot 1/3/7/8 的字节级断言与 fixture 一致 ✅

### op-geth 交叉验证

`extractL1GasParamsPostIsthmus`（`rollup_cost.go:507–542`）仅提取 **fee 相关** 6 字段（与 FB `applySetterIsthmus` 写入面相同）；metadata 字段（sequenceNumber、timestamp、number、hash、batcherHash）在 op-geth L1 cost 函数中亦 **不读取** ✅

---

## Step 3: L1Block predeploy 实现（deviation 深审）

### 架构

```
deposit/user CALL → executeMessage → OpHostExtension::tryChainPrecompile
  → (target == OP_L1_BLOCK_PREDEPLOY) → L1BlockPredeploy::dispatch
```

非链上 Solidity `L1Block` 合约执行；matrix 已标 **deviation**，有正向测试 `L1BlockPredeployTest`、`L1BlockGetterTest`。

### Setter — `setL1BlockValuesIsthmus`（`L1BlockPredeploy.cpp:51–78`）

| 规则 | FB | op-geth / `L1Block.sol` | 一致 |
|------|----|-------------------------|------|
| 仅 Depositor 可写 | `memcmp(sender, OP_DEPOSITOR_ACCOUNT)`；否则 REVERT + `NotDepositor()` selector `0x3cc50b45` | `setL1BlockValuesEcotone/Isthmus` assembly 同等检查 | ✅ |
| calldata ≥ 176 | `parseIsthmusL1Attributes` 不足则 REVERT | Solidity calldataload 越界 revert | ✅ |
| fee slot 写入 | 4 slot（见 Step 1） | Isthmus 经 Ecotone 写 basefee/blob/scalars + operator slot | ✅（fee 面） |
| metadata 写入 | **未写** number/timestamp/hash/sequenceNumber/batcherHash | `L1Block.sol:157–170` sstore 全套 Ecotone 字段 | 🟡 deviation |
| gas 消耗 | 返回 `msg.gas`（不 meter SSTORE） | 真实 EVM gas | 🟡 TE 简化（可接受） |

### Getter dispatch（`L1BlockPredeploy.cpp:89–117`）

| FB selector | 名称 | 返回值 |
|-------------|------|--------|
| `0x519b4bd3` | `l1BaseFee()` | slot 1 |
| `0xc5985918` | `baseFeeScalar()` | slot 3 [16:20) |
| `0x68d5dca6` | `blobBaseFeeScalar()` | slot 3 [20:24) |
| `0x84189161` | `l1BlobBaseFee()` | slot 7 |
| `0x4d5d9a2a` | `operatorFeeScalar()` | slot 8 [20:24) |
| `0x16d3bc7f` | `operatorFeeConstant()` | slot 8 [24:32) |

**🟡 Getter ABI 偏离链上 `IL1Block`：**

| 链上 L1Block（`IL1Block.sol`） | selector | FB 是否实现 |
|-------------------------------|----------|-------------|
| `basefee()` | `0x5cf24969` | ❌ |
| `blobBaseFee()` | `0xf8206140` | ❌ |
| `number()` / `timestamp()` / `hash()` / `sequenceNumber()` / `batcherHash()` | 各链上 selector | ❌ |
| `DEPOSITOR_ACCOUNT()` / `gasPayingToken*()` / `version()` 等 | — | ❌ |

FB 实现的 `l1BaseFee()` / `l1BlobBaseFee()` 命名与 selector 对应 **GasPriceOracle**（`0x4200…000F`），而非 L1Block 地址上的 `basefee()` / `blobBaseFee()`。

**TE 影响：** `loadOpStackFeeParams` / `l1CostFjord` / `operatorCostIsthmus` 直接读 storage，**不受** getter ABI 影响 ✅  
**兼容性影响：** 用户合约若 `CALL` L1Block.`basefee()` 将在 FB 路径 REVERT（unknown selector）→ 🟡

### Packing helpers（`L1BlockStorage.cpp:58–89`）

| 函数 | 布局 | op-geth |
|------|------|---------|
| `packL1FeeScalars` | bytes [16:24) big-endian u32×2 | `scalarSectionStart` + `ExtractL1FeeScalars` 族 |
| `packOperatorFeeParams` | bytes [20:32) u32 + u64 | `ExtractOperatorFeeParams` `[20:24)` / `[24:32)` |
| `unpack*` | 与 getter 一致 | 同 |

---

## Step 4: L1 attributes deposit 成功路径 + fee 联动

### E2E 链（`L1AttributesDepositTest.cpp`）

```
1. deposit tx (0x7E) from OP_DEPOSITOR_ACCOUNT → OP_L1_BLOCK_PREDEPLOY
   calldata = isthmus_l1_attributes.bin (176B)
2. opStackExecuteViaHost → OpHostExtension → L1BlockPredeploy::applySetterIsthmus
3. applyStateDiffToView — slot 1/3/7/8 持久化
4. 后续 user tx + rollupCostData + m_isIsthmus=true
5. 断言 OP_L1_FEE_RECIPIENT balance > 0
```

证明：attributes deposit 更新的 L1Block fee slots 被 `loadOpStackFeeParams`（`OpStackExecuteViaHost.cpp:87`）消费，Fjord L1 fee 路由生效 ✅

### 与 specs 行为对照

| 规则 | specs | FB | 判定 |
|------|-------|----|------|
| Isthmus 后续块使用 `setL1BlockValuesIsthmus()` | `l1-attributes.md` L39–40 | deposit calldata selector `0x098999be` | ✅ |
| Depositor 独占写 | `l1-block.md` i01-001 | setter + deposit sender 检查 | ✅ |
| Operator fee 参数来自 attributes | Isthmus 段 6 | slot 8 写入 | ✅ |
| 首块 / 升级 `setIsthmus()` 一次性迁移 | `predeploys.md` §setIsthmus | **未实现** | ⚪ TE 范围外（genesis/升级由链配置承担） |

### 交叉引用 Task 4（deposit orchestration）

L1 attributes 走同一 `opStackExecuteViaHost` deposit 分支（`:122–181`）：

| 项 | 对 L1 attributes 的影响 | Task 4 结论 |
|----|-------------------------|-------------|
| 成功 sender nonce +1 | attributes deposit 成功后 nonce 可能未递增 | 🔴 D4-1 |
| 失败 REVERT gasUsed | 无效 calldata REVERT 时 gasUsed=gasLimit | 🔴 D4-2（若走 EVM REVERT 路径） |
| entry 失败 | intrinsic/floor 失败时无 nonce bump | 🔴 D4-3 |

本 Task 测试 **未覆盖** deposit nonce / gasUsed（见 Part 3）。

---

## Step 5: L1 attributes deposit 失败路径

### FB（`L1AttributesDepositFailureTest.cpp`）

1. 先成功 deposit 写入 slot  
2. 再发送仅 4 字节 selector、无 payload 的 calldata  
3. 断言 `EVMC_REVERT` 且 `stateDiff` **不含** `OP_L1_BLOCK_PREDEPLOY`（slot 变更未 commit）

机制：`applySetterIsthmus` parse 失败 → REVERT；deposit 分支 `state.revert()`（`:173`）回滚 checkpoint ✅

### 对照 op-geth / Solidity

| 场景 | 期望 | FB |
|------|------|-----|
| calldata 过短 | REVERT，storage 不变 | ✅ |
| 非 Depositor caller | `NotDepositor()` REVERT | ✅ `L1BlockPredeployTest::setter_rejects_non_depositor_sender` |
| 失败 deposit nonce | +1（Regolith+） | 未测；Task 4 🔴 |
| 失败 deposit gasUsed | Regolith+ 实际 gas（EVM REVERT） | 未测；Task 4 🔴 |

---

## Part 1 — 合规矩阵行

| 能力 | 层级 | 清单来源 | Matrix 状态 | 深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|------|------|-----------|---------|--------------|---------|------|
| chain precompile routing (L1Block) | host extension | matrix:15 | deviation | 深审 | 🟡 **DONE_WITH_CONCERNS** | `l1-block.md` 全量接口；`predeploys.md` | `OpHostExtension.h:20–32` → `L1BlockPredeploy.cpp` | 地址/slot/fee 面一致；**非完整 Solidity 仿真**；getter ABI 偏离 | `L1BlockPredeployTest`, `L1BlockGetterTest` | `basefee()`/`blobBaseFee()` 等链上 getter；metadata slot |
| L1 attributes system deposit | orchestration | **增补 S2** | 待 matrix 合入 | 深审 | 🟡 **DONE_WITH_CONCERNS** | `isthmus/l1-attributes.md` | `parseIsthmusL1Attributes` + deposit → `applySetterIsthmus` | `extractL1GasParamsPostIsthmus` fee 字段一致 | `L1AttributesDepositTest`, `L1AttributesDepositFailureTest`, `isthmus_l1_attributes.bin` | deposit nonce/gas（Task 4）；operator fee 数值未断言 |

**Matrix patch 建议（S2）：**

```markdown
| L1 attributes system deposit | orchestration | explicit | `parseIsthmusL1Attributes`, deposit orchestration | `L1AttributesDepositTest`, `L1AttributesDepositFailureTest`, `isthmus_l1_attributes.bin` |
```

**Matrix Test ref 增补（L1Block 行）：** 加 `L1AttributesDepositTest`, `L1AttributesDepositFailureTest`。

---

## Part 2 — 偏离项详情

### D5-1 🟡 L1Block 非完整合约仿真（metadata storage 未写入）

- **现象：** `applySetterIsthmus` 仅写 slot 1/3/7/8；不更新 `number`、`timestamp`、`hash`、`sequenceNumber`、`batcherHash`（`L1Block.sol` Ecotone 段）。
- **规范：** `l1-block.md` `setL1BlockValuesEcotone/Isthmus` MUST 更新全套 L1 上下文。
- **金标准：** `L1Block.sol:155–193`；op-geth 链上 storage 布局。
- **TE 影响：** L1/operator **fee 计算不依赖** metadata（op-geth `extractL1GasParamsPostIsthmus` 亦只读 fee 字段）→ orchestration **可接受**。
- **兼容性影响：** 依赖 `L1Block.number()` / `hash()` 等的 L2 合约在 FB TE 路径读到 stale/zero → 🟡。
- **修复建议（若需链上 ABI  parity）：** 扩展 `applySetterIsthmus` 写入 Ecotone metadata slot；或文档化 TE 仅保证 fee oracle storage。

### D5-2 🟡 Getter selector 与链上 `IL1Block` 不一致

- **现象：** FB 在 `0x4200…0015` 实现 `l1BaseFee()`（`0x519b4bd3`）、`l1BlobBaseFee()`（`0x84189161`）；链上 L1Block 公开 `basefee()`（`0x5cf24969`）、`blobBaseFee()`（`0xf8206140`）。
- **金标准：** `IL1Block.sol`；cast sig 对照。
- **TE 影响：** fee orchestration 无影响（direct storage read）。
- **修复建议：** 增补 `basefee()` / `blobBaseFee()` dispatch（可保留 legacy alias）；或 alias 到同一 slot 读。

### D5-3 🟡 大量 L1Block 只读 API 未实现

- **缺失：** `number()`, `timestamp()`, `hash()`, `sequenceNumber()`, `batcherHash()`, `DEPOSITOR_ACCOUNT()`, `gasPayingToken*()`, `isCustomGasToken()`, `version()`, Ecotone 前 legacy getter 等。
- **严重度：** 🟡（TE baseline 当前无 consumer；derivation/batcher 验证不在 bcos-evm 范围）。

### D5-4 🟡 L1 attributes deposit E2E 断言不完整

- **现象：** `L1AttributesDepositTest` 仅断言 L1 fee recipient balance > 0，未断言具体 `l1Fee` 数值、operator fee、或 slot 与 fixture 一致（后者由 `L1BlockPredeployTest` 覆盖）。
- **修复建议：** 扩展 E2E：对 `output.receiptMeta` / 精确 fee 与 op-geth 向量对照。

### D5-5 🔴（交叉 Task 4）deposit nonce / gasUsed 语义

- **现象：** L1 attributes deposit 继承 Task 4 D4-1/D4-2 问题。
- **修复：** 见 `task4-deposit.md` P0 顺序；补 `L1AttributesDepositTest` nonce / `depositNonce` 断言。

---

## Part 3 — 测试断言审计

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `L1BlockPredeployTest.cpp` | `setter_unpacks_isthmus_fixture_into_slots` | ✅ 有效 | `isthmus/l1-attributes.md` slot 1/3/7/8 + fixture | 字节级对齐 |
| `L1BlockPredeployTest.cpp` | `setter_rejects_non_depositor_sender` | ✅ 有效 | `NotDepositor()` / i01-001 | REVERT + slot 0 |
| `L1BlockPredeployTest.cpp` | `getters_return_slot_values_after_setter` | 🟡 部分 | FB 自定义 getter ABI | 未测链上 `basefee()` selector |
| `L1BlockGetterTest.cpp` | `op_host_extension_dispatches_l1block_getter` | ✅ 有效 | HostExtension 路由 | E2E `l1BaseFee()` @ predeploy |
| `L1AttributesDepositTest.cpp` | `l1_attributes_deposit_updates_l1block_and_affects_following_user_tx` | 🟡 部分 | fee 联动 smoke | balance>0 非数值 parity；无 operator fee 断言 |
| `L1AttributesDepositFailureTest.cpp` | `failed_l1_attributes_deposit_does_not_commit_slot_changes` | ✅ 有效 | REVERT 不 commit slot | 未断言 nonce/gasUsed |

**Fixture：** `isthmus_l1_attributes.bin` — 176B，selector + 全字段填充；与 spec offset 表一致 ✅

---

## 汇总

| 子域 | vs op-geth / specs (Isthmus) | 严重度 |
|------|------------------------------|--------|
| Predeploy / Depositor 地址 | 一致 | ✅ |
| Fee storage slot 布局 | 一致 | ✅ |
| `ISTHMUS_L1_ATTRIBUTES_LEN` / selector | 一致 | ✅ |
| calldata parse 字段顺序 | 一致 | ✅ |
| Setter fee slot 写入 + packing | 一致 | ✅ |
| Depositor 鉴权 + NotDepositor | 一致 | ✅ |
| L1 attributes → Fjord L1 fee 联动 | smoke 有效 | ✅ |
| 失败 deposit 不 commit slot | 一致 | ✅ |
| **Metadata slot 写入** | FB 省略 | 🟡 |
| **L1Block getter ABI** | FB 用 GPO 命名，缺 `basefee()` 等 | 🟡 |
| **deposit nonce / gasUsed** | Task 4 🔴 继承 | 🔴 |
| **`setIsthmus()` 升级迁移** | 未实现 | ⚪ TE 范围外 |

**Task 5 状态：** **DONE_WITH_CONCERNS**（fee orchestration 核心对齐 ✅；deviation 面 🟡×4；deposit 交叉 🔴×1）

**P1 动作：** D5-2 增补 `basefee()`/`blobBaseFee()` getter；D5-4 加强 E2E fee 数值断言；D5-5 随 Task 4 P0 修复 deposit nonce/gas。
