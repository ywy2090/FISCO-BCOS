# OPStack L1Block 全量 IL1Block 对齐（OP-14）设计规格

**日期：** 2026-06-20  
**状态：** 已评审（方案 C + grill 补丁 + R1–R4 合入）  
**前置：** P0 remediation（OP-01–09 + OP-09b）已完成；P1 OP-11–13、OP-15 已完成或进行中  
**取代：** `2026-06-20-opstack-isthmus-p1-quality-design.md` 中 OP-14「收窄」一行  
**审计引用：** task5 D5-1、D5-2、D5-3；`bcos-evm/docs/audits/_work/task5-l1block-attributes.md`

---

## 1. 目标

闭合 OPStack Isthmus L1Block predeploy 与 optimism `IL1Block` / `L1Block.sol` 之间的 deviation：

1. **D5-1** — `setL1BlockValuesIsthmus` 写入 Ecotone 全套 metadata storage（slot 0/2/3/4 及 slot 3 的 `sequenceNumber`）
2. **D5-2** — 实现链上标准 `basefee()` / `blobBaseFee()` selector
3. **D5-3** — 实现 `IL1Block` 其余 view/pure getter（含 legacy slot 只读、mapping、`gasPayingToken*` 等）

使 L2 合约在 TE 路径上可通过标准 ABI 读取 L1 上下文；**不改变** L1/operator fee orchestration（仍 direct storage read slot 1/3/7/8）。

### 1.1 Parity 定义（非 bytecode parity）

本 epic 交付的是 **Isthmus-era IL1Block call-surface parity**：

- **写：** `setL1BlockValuesIsthmus` 的 storage 副作用与 `L1Block.sol` Ecotone/Isthmus 段一致
- **读：** `IL1Block` 列出的 view/pure getter（§5）在 `0x4200…0015` 可 CALL 且 ABI 输出正确
- **非目标：** 完整 Solidity 合约仿真（proxy、`setFeature`、events、gas metering、Bedrock/Jovian setter）

**假设：** 非 custom gas token 链（`isCustomGasToken() == false`）；`gasPayingToken*` 返回 ETH 常量。

---

## 2. 范围

### 2.1 包含

| 类别 | 交付 |
|------|------|
| Setter 副作用 | 扩展 `applySetterIsthmus` 写入 slot 0、2、3（含 sequenceNumber）、4、1、7、8 |
| View getter | `IL1Block` 全部 state-backed getter（§5.1） |
| Pure getter | `DEPOSITOR_ACCOUNT`、`version`、`gasPayingToken*`、`isCustomGasToken`（§5.3） |
| Mapping getter | `isFeatureEnabled(bytes32)`（§5.4） |
| Legacy alias | 保留 `l1BaseFee()` / `l1BlobBaseFee()`，与 `basefee()` / `blobBaseFee()` 读同一 slot |
| 测试 | `L1BlockPredeployTest` 覆盖 §5 全表；`L1BlockGetterTest` E2E smoke |
| Selector 常量 | `L1BlockSelectors.h`（**必选**，避免 magic number 漂移） |

### 2.2 不包含（defer / known gap）

| API / 范围 | 原因 |
|------------|------|
| **GasPriceOracle predeploy** `0x4200…000F` | **本 epic out of scope**；TE 不仿真 GPO；`l1BaseFee()` / `l1BlobBaseFee()` 仅作为 **L1Block 地址** 上的 legacy alias，不等价于 GPO 合约 |
| `setL1BlockValues`（Bedrock ABI） | 非 Isthmus deposit 路径 |
| `setL1BlockValuesEcotone()` / `setL1BlockValuesJovian()` 独立 dispatch | Isthmus deposit 已用 `setL1BlockValuesIsthmus`；Jovian 另开 epic |
| `setFeature(bytes32)` | 写路径 + ProxyAdmin 鉴权链 |
| `__constructor__` | genesis / 链配置职责 |
| `IProxyAdminOwnedBase::proxyAdmin()` / `proxyAdminOwner()` | 依赖 proxy 基础设施；TE 未仿真 proxy 存储 |

**Legacy slot 5/6：** TE genesis 不迁移 Bedrock `l1FeeOverhead` / `l1FeeScalar`；getter 读 TE storage 真值（默认 0），非主网历史 state 回放。

### 2.3 计费路径

`loadOpStackFeeParams` / `l1CostFjord` / `operatorCostIsthmus` **不修改**；继续读 slot 1/3/7/8。

---

## 3. 实现策略

**推荐：表驱动 dispatch + 专用 handler**

- `L1BlockStorage` — slot 打包/解包、ABI string/tuple 编码、`isFeatureEnabled` mapping 读
- `L1BlockPredeploy::dispatch` — 数值 getter 表驱动；string / `gasPayingToken()` 专用 handler
- `L1BlockSelectors.h` — selector 常量集中定义（**必选**）

不采用：每个 getter 独立 `case`（文件过长）；宏注册框架（与现有风格不符）。

---

## 4. Storage 布局

对照 `optimism/packages/contracts-bedrock/src/L2/L1Block.sol` Ecotone assembly 与 op-geth `core/types/rollup_cost.go`。

| Slot | 常量（新增/已有） | 内容 | Isthmus setter 写入 |
|------|-------------------|------|---------------------|
| 0 | `L1_NUMBER_TIMESTAMP_SLOT` | `timestamp` bytes [16:24) + `number` bytes [24:32) | ✅ calldata 20–35 |
| 1 | `L1_BASE_FEE_SLOT` | `basefee` uint256 | ✅ 已有 |
| 2 | `L1_HASH_SLOT` | `hash` bytes32 | ✅ calldata 100–131 |
| 3 | `L1_FEE_SCALARS_SLOT` | `baseFeeScalar` [16:20) + `blobBaseFeeScalar` [20:24) + `sequenceNumber` [24:32) | ✅ 扩展 sequenceNumber |
| 4 | `L1_BATCHER_HASH_SLOT` | `batcherHash` bytes32 | ✅ calldata 132–163 |
| 5 | `L1_FEE_OVERHEAD_SLOT` | legacy `l1FeeOverhead` | ❌ 保持 0 |
| 6 | `L1_FEE_SCALAR_LEGACY_SLOT` | legacy `l1FeeScalar` | ❌ 保持 0 |
| 7 | `L1_BLOB_BASE_FEE_SLOT` | `blobBaseFee` | ✅ 已有 |
| 8 | `OPERATOR_FEE_PARAMS_SLOT` | `daFootprintGasScalar` [18:20) + `operatorFeeScalar` [20:24) + `operatorFeeConstant` [24:32)（Solidity 从 slot 末端 packing） | ✅ operator 字段已有；Isthmus setter **不 SET** `daFootprint`（保持 0） |
| 9 | `L1_FEATURE_ENABLED_MAPPING_SLOT` | `mapping(bytes32 => bool) isFeatureEnabled` | setter 不写 |

### 4.1 打包函数

| 函数 | 行为 |
|------|------|
| `packL1NumberTimestamp(uint64 timestamp, uint64 number)` | slot 0：与 `sstore(number.slot, shr(128, calldataload(20)))` 一致 |
| `packL1FeeScalarsSlot(uint32 baseFeeScalar, uint32 blobBaseFeeScalar, uint64 sequenceNumber)` | **唯一**写 slot 3 的入口；写入 bytes [16:32) |
| `packOperatorFeeParams` | 已有，不变 |

**API 迁移：** 删除或内联旧 `packL1FeeScalars`（仅 scalars、不含 `sequenceNumber`），避免其他调用方写 slot 3 时清零 `sequenceNumber`。

Setter 写入顺序：0 → 1 → 2 → 3 → 4 → 7 → 8（与 Ecotone assembly 字段一致；顺序不影响最终态）。`set_storage` 无失败分支；deposit REVERT 时 checkpoint 回滚，**无 partial commit**。

### 4.2 Calldata 布局（已有 `parseIsthmusL1Attributes`）

176 字节；selector `0x098999be`；字段 offset 见 `isthmus/l1-attributes.md` 与 `isthmus_l1_attributes.bin` fixture。

### 4.3 Fixture 金标准（`isthmus_l1_attributes.bin`）

实现与测试 **直接引用本表**，勿从 fixture 二次推导。

#### 4.3.1 Setter 后 storage 字节（`setter_unpacks_isthmus_fixture_into_slots`）

| 位置 | 期望字节（hex，连续书写） |
|------|---------------------------|
| slot 0 bytes [16:24) `timestamp` | `1112131415161718` |
| slot 0 bytes [24:32) `number` | `2122232425262728` |
| slot 3 bytes [16:20) `baseFeeScalar` | `11223344` |
| slot 3 bytes [20:24) `blobBaseFeeScalar` | `55667788` |
| slot 3 bytes [24:32) `sequenceNumber` | `0102030405060708` |
| slot 1 全 slot `basefee` | 数值 `0x0123456789abcdef`（现有测试已覆盖） |
| slot 2 全 slot `hash` | `0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20` |
| slot 4 全 slot `batcherHash` | `2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40` |
| slot 7 全 slot `blobBaseFee` | 数值 `0x0fedcba987654321`（现有测试已覆盖） |
| slot 8 bytes [20:32) operator | 现有测试已覆盖 |
| slot 8 bytes [18:20) `daFootprintGasScalar` | `0000`（Isthmus 未 SET） |

#### 4.3.2 Getter ABI output（`getters_return_slot_values_after_setter`）

经 setter 写入 fixture 后，下列为期望 `output`（hex，无 `0x` 前缀，32 字节 word 右对齐规则）：

| Getter | 期望 output hex |
|--------|-----------------|
| `number()` | `0000000000000000000000000000000000000000000000002122232425262728` |
| `timestamp()` | `0000000000000000000000000000000000000000000000001112131415161718` |
| `sequenceNumber()` | `0000000000000000000000000000000000000000000000000102030405060708` |
| `basefee()` / `l1BaseFee()` | `0000000000000000000000000000000000000000000000000123456789abcdef` |
| `blobBaseFee()` / `l1BlobBaseFee()` | `0000000000000000000000000000000000000000000000000fedcba987654321` |
| `baseFeeScalar()` | `0000000000000000000000000000000000000000000000000000000011223344` |
| `blobBaseFeeScalar()` | `0000000000000000000000000000000000000000000000000000000055667788` |
| `hash()` | `0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20` |
| `batcherHash()` | `2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40` |
| `l1FeeOverhead()` / `l1FeeScalar()` | `0000000000000000000000000000000000000000000000000000000000000000` |
| `operatorFeeScalar()` | `00000000000000000000000000000000000000000000000000000000a1b2c3d4` |
| `operatorFeeConstant()` | `0000000000000000000000000000000000000000000000000102030405060708` |
| `daFootprintGasScalar()` | `0000000000000000000000000000000000000000000000000000000000000000` |

---

## 5. Getter dispatch

### 5.1 View — 读 storage

| 函数 | Selector | 返回类型 | 读法 |
|------|----------|----------|------|
| `number()` | `0x8381f58a` | uint64 | slot 0 [24:32) |
| `timestamp()` | `0xb80777ea` | uint64 | slot 0 [16:24) |
| `basefee()` | `0x5cf24969` | uint256 | slot 1 |
| `hash()` | `0x09bd5a60` | bytes32 | slot 2 |
| `sequenceNumber()` | `0x64ca23ef` | uint64 | slot 3 [24:32) |
| `blobBaseFeeScalar()` | `0x68d5dca6` | uint32 | slot 3 [20:24) |
| `baseFeeScalar()` | `0xc5985918` | uint32 | slot 3 [16:20) |
| `batcherHash()` | `0xe81b2c6d` | bytes32 | slot 4 |
| `l1FeeOverhead()` | `0x8b239f73` | uint256 | slot 5 |
| `l1FeeScalar()` | `0x9e8c4966` | uint256 | slot 6 |
| `blobBaseFee()` | `0xf8206140` | uint256 | slot 7 |
| `operatorFeeScalar()` | `0x4d5d9a2a` | uint32 | slot 8 [20:24) |
| `operatorFeeConstant()` | `0x16d3bc7f` | uint64 | slot 8 [24:32) |
| `daFootprintGasScalar()` | `0xfe3d5710` | uint16 | slot 8 [18:20) |

### 5.2 Legacy alias（保留）

| 函数 | Selector | 等价 |
|------|----------|------|
| `l1BaseFee()` | `0x519b4bd3` | `basefee()` |
| `l1BlobBaseFee()` | `0x84189161` | `blobBaseFee()` |

### 5.3 Pure 常量

| 函数 | Selector | 返回值 |
|------|----------|--------|
| `DEPOSITOR_ACCOUNT()` | `0xe591b282` | `OP_DEPOSITOR_ACCOUNT` |
| `isCustomGasToken()` | `0x21326849` | `false` |
| `gasPayingToken()` | `0x4397dfef` | `(0xEeeeeEeeeEeEeeEeEeEeeEEEeeeeEeeeeeeeEEeE, 18)` |
| `gasPayingTokenName()` | `0xd8444715` | `"Ether"` |
| `gasPayingTokenSymbol()` | `0x550fcdc9` | `"ETH"` |
| `version()` | `0x54fd4d50` | `"1.9.0"` |

`gasPayingToken` 地址取自 optimism `Constants.ETHER`。

### 5.3.1 ABI 金标准（测试全量 hex 比对）

| 函数 | 期望 `output`（hex，无 `0x` 前缀） |
|------|-----------------------------------|
| `version()` | `00000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000005312e392e30000000000000000000000000000000000000000000000000000000` |
| `gasPayingTokenName()` | `000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000054574686572000000000000000000000000000000000000000000000000000000` |
| `gasPayingTokenSymbol()` | `000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000034554480000000000000000000000000000000000000000000000000000000000` |
| `gasPayingToken()` | `000000000000000000000000eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee0000000000000000000000000000000000000000000000000000000000000012` |
| `DEPOSITOR_ACCOUNT()` | `000000000000000000000000deaddeaddeaddeaddeaddeaddeaddeaddead0001` |
| `isCustomGasToken()` | `0000000000000000000000000000000000000000000000000000000000000000` |

---

### 5.4 Mapping

| 函数 | Selector | 行为 |
|------|----------|------|
| `isFeatureEnabled(bytes32)` | `0x47af267b` | `slot = keccak256(abi.encode(key, uint256(9)))` 后 SLOAD；0 → `false` |

`key` 为 calldata bytes [4:36]。因 defer `setFeature`，本 epic **仅保证** 未写入条目返回 `false`（无 `true` 路径测试）。

calldata 须 ≥ 36 字节（selector + key）；不足则 REVERT。

#### 5.4.1 Mapping slot 计算（实现约束）

`isFeatureEnabled` 的 storage key 计算在 **`L1BlockStorage.cpp`** 内完成，**不**向 `L1BlockPredeploy::dispatch` 传入 `Hash` / `EthHost`：

1. 构造 64 字节 buffer：`key`（32B，calldata [4:36]）‖ `uint256(9)` big-endian（32B）— 等价 Solidity `abi.encode(bytes32 key, uint256(9))`
2. `keccak256(buffer)` → mapping entry slot
3. `state.get_storage(OP_L1_BLOCK_PREDEPLOY, slot)`；零值 → getter 返回 `false`

**哈希实现：** 复用仓库已有 Keccak-256（例如 `wedpr_keccak256_hash`，见 `bcos-evm/eth/precompiled/EthBuiltinRegistry.cpp` `ecrecover` 路径），或 `bcos-crypto` 的 `Hash::hash`。单元测试可对固定 `key` 断言 mapping slot 与 SLOAD 结果。

### 5.5 ABI 编码规则

- `uint256` / `bytes32`：32 字节 big-endian
- `uint64` / `uint32` / `uint16`：右对齐于 32 字节 word
- `string`：offset(32) + length + data（32 字节 padding）
- `gasPayingToken()`：两个 32 字节 word（address 右对齐 + uint8 右对齐）

### 5.6 未知 selector

`EVMC_REVERT`（与当前行为一致）。

Gas：TE 简化 — 返回 `msg.gas`，不 meter SSTORE/CALL。

---

## 6. 数据流

```
L1 attributes deposit (type 0x7E)
  → opStackExecuteViaHost
  → OpHostExtension::tryChainPrecompile (target == OP_L1_BLOCK_PREDEPLOY)
  → L1BlockPredeploy::dispatch (0x098999be)
  → applySetterIsthmus → set_storage slots 0,1,2,3,4,7,8

L2 contract CALL L1Block.number()
  → 同上路由
  → dispatch table → unpack slot 0 → ABI encode → EVMC_SUCCESS
```

---

## 7. 文件变更

| 文件 | 变更 |
|------|------|
| `bcos-evm/opstack/OpStackConstants.h` | slot 0/2/4/5/6/9 常量 |
| `bcos-evm/opstack/l1/L1BlockStorage.h` | pack/unpack 扩展；`encodeAbiString`；`readFeatureEnabled`（含 Keccak mapping slot，§5.4.1） |
| `bcos-evm/opstack/l1/L1BlockStorage.cpp` | 实现；**Keccak-256 在此文件**（`wedpr_keccak256_hash` 或 `bcos-crypto::Hash`） |
| `bcos-evm/opstack/l1/L1BlockPredeploy.cpp` | setter 扩展；getter 表 + handlers |
| `bcos-evm/test/opstack/L1BlockPredeployTest.cpp` | metadata slot + 全 getter |
| `bcos-evm/test/opstack/L1BlockGetterTest.cpp` | `basefee()`、`number()` E2E smoke |
| `bcos-evm/opstack/l1/L1BlockSelectors.h` | selector 常量（**新增，必选**） |

---

## 8. 错误处理

| 场景 | 行为 |
|------|------|
| Setter 非 Depositor | REVERT + `NotDepositor()` `0x3cc50b45` |
| Setter calldata &lt; 176 | REVERT |
| Getter 成功 | ABI 编码输出 |
| `isFeatureEnabled` calldata 不足 | REVERT |
| 未知 selector | REVERT |

---

## 9. 测试与 Done 定义

### 9.1 必过测试

金标准数值见 **§4.3**（storage 字节）、**§5.3.1**（pure getter hex）、**§4.3.2**（view getter hex）。

1. `setter_unpacks_isthmus_fixture_into_slots` — 按 §4.3.1 字节断言（含 slot 0/2/3[24:32)/4）
2. `getters_return_slot_values_after_setter` — 按 §4.3.2 全量 output hex；§5.2 alias 与对应标准 getter 相同
3. `pure_getters_match_l1block_constants` — §5.3.1 全表（含 `DEPOSITOR_ACCOUNT` / `isCustomGasToken`）
4. `isFeatureEnabled_returns_false_by_default` — 任意 `bytes32` key（mapping 读法 §5.4.1）
5. `L1BlockGetterTest` — E2E smoke：`basefee()` + `number()` 经 `OpHostExtension`

Fixture：`bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin`（176B）。

### 9.2 Done

- 代码 + 上述测试 PASS
- **Work-list（R4）：** `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-work-list.md` — 若文件 **不存在则创建** 并登记 OP-14 条目；若已存在则将 OP-14 标为 `[x]`（含验收测试列指向 §9.1）
- `bcos-evm/docs/audits/_work/task5-l1block-attributes.md` — D5-1/2/3 标记闭合
- `bcos-evm/capability-matrix.md` L1Block 行 — 注明：无 `proxyAdmin` / `setFeature` / **GPO `0x4200…000F`**

### 9.3 验收命令

```bash
ctest -R 'L1BlockPredeployTest|L1BlockGetterTest' --output-on-failure
```

---

## 10. 参考

| 来源 | 路径 |
|------|------|
| L1Block 合约 | `optimism/.../src/L2/L1Block.sol` |
| IL1Block 接口 | `optimism/.../interfaces/L2/IL1Block.sol` |
| op-geth slots | `op-geth/core/types/rollup_cost.go` |
| Isthmus attributes spec | optimism `protocol/isthmus/l1-attributes.md` |
| FB 审计笔记 | `bcos-evm/docs/audits/_work/task5-l1block-attributes.md` |
| 现有实现 | `bcos-evm/opstack/l1/L1BlockPredeploy.cpp` |

---

## 11. 后续（本 spec 外）

- **GasPriceOracle** predeploy `0x4200…000F` 仿真（与 L1Block legacy alias 区分）
- Jovian `setL1BlockValuesJovian` + `daFootprintGasScalar` setter
- `IProxyAdminOwnedBase` getter 仿真
- `setFeature` 写路径
- OP-10 capability-matrix 行更新
