# OPStack L1Block IL1Block Call-Surface Parity（OP-14）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `0x4200…0015` 实现 Isthmus L1Block storage 全量写入 + `IL1Block` getter call-surface parity，闭合审计 D5-1/2/3。

**Architecture:** `L1BlockStorage` 负责 slot 打包/解包、ABI string/tuple、`isFeatureEnabled` Keccak mapping；`L1BlockPredeploy::dispatch` 表驱动数值 getter + 专用 string/tuple handler；selector 常量在 `L1BlockSelectors.h`。计费 orchestration **不改动**。

**Tech Stack:** C++17、Boost.Test、evmc/evmone、`ethash::keccak256`（`evmone_precompiles/keccak.hpp`）、`bcos-evm-op` 静态库。

**设计 spec：** `docs/superpowers/specs/2026-06-20-opstack-l1block-full-parity-design.md`

## Global Constraints

- **Parity 范围：** Isthmus-era IL1Block **call-surface**（非 bytecode parity）；见 spec §1.1。
- **GPO `0x4200…000F`：** **out of scope**；`l1BaseFee()` / `l1BlobBaseFee()` 仅在 L1Block 地址作 legacy alias。
- **计费：** 不修改 `loadOpStackFeeParams` / `OpStackFee.cpp`；仅读 slot 1/3/7/8。
- **Gas：** TE 简化 — predeploy 返回 `msg.gas`，不 meter SSTORE。
- **金标准：** 测试断言 **直接引用 spec §4.3 / §5.3.1 / §4.3.2**，勿重新推导。
- **命令前缀：** `rtk`；构建目录假定为仓库根下 `build/`（与现有 opstack 测试一致）。
- **Defer：** `setFeature`、`proxyAdmin*`、Bedrock/Jovian setter、GPO predeploy。

## 文件结构（本 epic 锁定）

| 文件 | 职责 |
|------|------|
| `bcos-evm/opstack/OpStackConstants.h` | slot 0/2/4/5/6/9 常量 |
| `bcos-evm/opstack/l1/L1BlockSelectors.h` | 全部 getter/setter selector（**新增**） |
| `bcos-evm/opstack/l1/L1BlockStorage.h/cpp` | parse（已有）、pack/unpack、ABI encode、mapping SLOAD |
| `bcos-evm/opstack/l1/L1BlockPredeploy.cpp` | `applySetterIsthmus` + getter dispatch |
| `bcos-evm/test/opstack/L1BlockPredeployTest.cpp` | storage 字节 + getter hex 金标准 |
| `bcos-evm/test/opstack/L1BlockGetterTest.cpp` | `OpHostExtension` E2E smoke |

`bcos-evm/CMakeLists.txt` 已含 `L1BlockStorage.cpp` / `L1BlockPredeploy.cpp`；**无需**改 CMake 除非新增独立测试目标。

---

### Task 1: Slot 常量 + Selector 头文件

**Files:**
- Modify: `bcos-evm/opstack/OpStackConstants.h`
- Create: `bcos-evm/opstack/l1/L1BlockSelectors.h`

**Interfaces — Produces:**
- `L1_NUMBER_TIMESTAMP_SLOT` = 0, `L1_HASH_SLOT` = 2, `L1_BATCHER_HASH_SLOT` = 4
- `L1_FEE_OVERHEAD_SLOT` = 5, `L1_FEE_SCALAR_LEGACY_SLOT` = 6
- `L1_FEATURE_ENABLED_MAPPING_SLOT` = 9
- `namespace L1BlockSelector` 内 `constexpr uint32_t`（与 spec §5 一致）

- [ ] **Step 1: 添加 slot 常量**

在 `OpStackConstants.h` `OPERATOR_FEE_PARAMS_SLOT` 附近追加：

```cpp
inline constexpr u256 L1_NUMBER_TIMESTAMP_SLOT{0};
inline constexpr u256 L1_HASH_SLOT{2};
inline constexpr u256 L1_BATCHER_HASH_SLOT{4};
inline constexpr u256 L1_FEE_OVERHEAD_SLOT{5};
inline constexpr u256 L1_FEE_SCALAR_LEGACY_SLOT{6};
inline constexpr u256 L1_FEATURE_ENABLED_MAPPING_SLOT{9};
```

- [ ] **Step 2: 创建 `L1BlockSelectors.h`**

```cpp
#pragma once
#include <cstdint>

namespace bcos::evm::l1block
{
inline constexpr uint32_t kSetL1BlockValuesIsthmus = 0x098999be;
inline constexpr uint32_t kNotDepositor = 0x3cc50b45;
// View getters — spec §5.1
inline constexpr uint32_t kNumber = 0x8381f58a;
inline constexpr uint32_t kTimestamp = 0xb80777ea;
inline constexpr uint32_t kBasefee = 0x5cf24969;
inline constexpr uint32_t kHash = 0x09bd5a60;
inline constexpr uint32_t kSequenceNumber = 0x64ca23ef;
inline constexpr uint32_t kBlobBaseFeeScalar = 0x68d5dca6;
inline constexpr uint32_t kBaseFeeScalar = 0xc5985918;
inline constexpr uint32_t kBatcherHash = 0xe81b2c6d;
inline constexpr uint32_t kL1FeeOverhead = 0x8b239f73;
inline constexpr uint32_t kL1FeeScalar = 0x9e8c4966;
inline constexpr uint32_t kBlobBaseFee = 0xf8206140;
inline constexpr uint32_t kOperatorFeeScalar = 0x4d5d9a2a;
inline constexpr uint32_t kOperatorFeeConstant = 0x16d3bc7f;
inline constexpr uint32_t kDaFootprintGasScalar = 0xfe3d5710;
// Legacy alias §5.2
inline constexpr uint32_t kL1BaseFee = 0x519b4bd3;
inline constexpr uint32_t kL1BlobBaseFee = 0x84189161;
// Pure §5.3
inline constexpr uint32_t kDepositorAccount = 0xe591b282;
inline constexpr uint32_t kIsCustomGasToken = 0x21326849;
inline constexpr uint32_t kGasPayingToken = 0x4397dfef;
inline constexpr uint32_t kGasPayingTokenName = 0xd8444715;
inline constexpr uint32_t kGasPayingTokenSymbol = 0x550fcdc9;
inline constexpr uint32_t kVersion = 0x54fd4d50;
// Mapping §5.4
inline constexpr uint32_t kIsFeatureEnabled = 0x47af267b;
}  // namespace bcos::evm::l1block
```

- [ ] **Step 3: 编译 smoke**

```bash
rtk cmake --build build --target bcos-evm-op -j8
```

Expected: 成功（仅头文件 + 常量，无行为变更）。

---

### Task 2: L1BlockStorage — pack / unpack / ABI encode / mapping

**Files:**
- Modify: `bcos-evm/opstack/l1/L1BlockStorage.h`
- Modify: `bcos-evm/opstack/l1/L1BlockStorage.cpp`

**Interfaces — Consumes:** Task 1 slot 常量、`L1_FEATURE_ENABLED_MAPPING_SLOT`。

**Interfaces — Produces:**

```cpp
evmc_bytes32 packL1NumberTimestamp(uint64_t timestamp, uint64_t number);
evmc_bytes32 packL1FeeScalarsSlot(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar, uint64_t sequenceNumber);
// 删除 packL1FeeScalars — 由 packL1FeeScalarsSlot 取代

u256 unpackNumber(evmc_bytes32 const& packed);
u256 unpackTimestamp(evmc_bytes32 const& packed);
u256 unpackSequenceNumber(evmc_bytes32 const& packed);
u256 unpackDaFootprintGasScalar(evmc_bytes32 const& packed);  // bytes [18:20)

bytes encodeAbiString(std::string_view value);
bytes encodeAbiAddressUint8(evmc_address addr, uint8_t decimals);
bytes encodeAbiAddress(evmc_address addr);  // 32-byte word, 右对齐

bool readFeatureEnabled(state::State& state, evmc_bytes32 const& key);
```

- [ ] **Step 1: 实现 `packL1NumberTimestamp`**

slot 0：`bytes [16:24)` = timestamp BE u64；`bytes [24:32)` = number BE u64。

```cpp
evmc_bytes32 packL1NumberTimestamp(uint64_t timestamp, uint64_t number)
{
    evmc_bytes32 out{};
    for (size_t i = 0; i < 8; ++i)
    {
        out.bytes[16 + i] = static_cast<uint8_t>((timestamp >> (56 - i * 8)) & 0xff);
        out.bytes[24 + i] = static_cast<uint8_t>((number >> (56 - i * 8)) & 0xff);
    }
    return out;
}
```

- [ ] **Step 2: 将 `packL1FeeScalars` 替换为 `packL1FeeScalarsSlot`**

写入 bytes [16:32)（scalars + sequenceNumber）；**删除**旧 `packL1FeeScalars` 声明与实现。

- [ ] **Step 3: 实现 unpack 族**

```cpp
u256 unpackNumber(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = 24; i < 32; ++i)
        value = (value << 8) | (u256)packed.bytes[i];
    return value;
}

u256 unpackTimestamp(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = 16; i < 24; ++i)
        value = (value << 8) | (u256)packed.bytes[i];
    return value;
}

u256 unpackSequenceNumber(evmc_bytes32 const& packed)
{
    u256 value = 0;
    for (size_t i = 24; i < 32; ++i)
        value = (value << 8) | (u256)packed.bytes[i];
    return value;
}

u256 unpackDaFootprintGasScalar(evmc_bytes32 const& packed)
{
    return ((u256)packed.bytes[18] << 8) | (u256)packed.bytes[19];
}
```

- [ ] **Step 4: 实现 `encodeAbiString`**

标准 ABI dynamic string：offset(32) + length + data（32 字节 padding）。对照 spec §5.3.1 四条 hex。

- [ ] **Step 5: 实现 `encodeAbiAddress` / `encodeAbiAddressUint8`**

`gasPayingToken()` 金标准 hex：`000000000000000000000000eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee0000000000000000000000000000000000000000000000000000000000000012`

- [ ] **Step 6: 实现 `readFeatureEnabled`**

```cpp
#include <evmone_precompiles/keccak.hpp>

bool readFeatureEnabled(state::State& state, evmc_bytes32 const& key)
{
    uint8_t buf[64];
    std::memcpy(buf, key.bytes, 32);
    // uint256(9) big-endian in second word
    for (size_t i = 0; i < 32; ++i)
        buf[32 + i] = (i == 31) ? 9 : 0;
    auto const hash = ethash::keccak256(buf, 64);
    evmc_bytes32 slot{};
    std::memcpy(slot.bytes, hash.bytes, 32);
    return !state::isZeroBytes32(state.get_storage(OP_L1_BLOCK_PREDEPLOY, slot));
}
```

- [ ] **Step 7: 编译**

```bash
rtk cmake --build build --target bcos-evm-op -j8
```

Expected: PASS。

---

### Task 3: Setter metadata 写入 + storage 测试（TDD）

**Files:**
- Modify: `bcos-evm/opstack/l1/L1BlockPredeploy.cpp`
- Modify: `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`

**Interfaces — Consumes:** Task 2 `packL1NumberTimestamp`, `packL1FeeScalarsSlot`。

- [ ] **Step 1: 扩展失败测试 `setter_unpacks_isthmus_fixture_into_slots`**

在现有 slot 1/3/7/8 断言后，按 spec §4.3.1 增加：

```cpp
auto const numberTs = state.get_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT));
for (size_t i = 0; i < 8; ++i)
    BOOST_CHECK_EQUAL(numberTs.bytes[16 + i], static_cast<uint8_t>(0x11 + i));  // 或逐字节表
// slot 2 hash 32 字节、slot 4 batcherHash、slot 3 [24:32) sequenceNumber
// slot 8 [18:20) daFootprint == 0x00, 0x00
```

（推荐：用 `std::array<uint8_t,8>` 与 spec hex 表逐字节 `BOOST_CHECK_EQUAL`，避免算术推导。）

- [ ] **Step 2: 运行测试确认 FAIL**

```bash
rtk test ./build/bcos-evm/test/L1BlockPredeployTest
```

Expected: FAIL — metadata slot 仍为 0。

- [ ] **Step 3: 扩展 `applySetterIsthmus`**

```cpp
state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_NUMBER_TIMESTAMP_SLOT),
    packL1NumberTimestamp(parsed->timestamp, parsed->l1BlockNumber));
state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_HASH_SLOT), parsed->hash);
state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_FEE_SCALARS_SLOT),
    packL1FeeScalarsSlot(parsed->baseFeeScalar, parsed->blobBaseFeeScalar, parsed->sequenceNumber));
state.set_storage(OP_L1_BLOCK_PREDEPLOY, state::toEvmC(L1_BATCHER_HASH_SLOT), parsed->batcherHash);
// 保留已有 slot 1/7/8 写入；顺序 0→1→2→3→4→7→8
```

将 `packL1FeeScalars(...)` 调用改为 `packL1FeeScalarsSlot(...)`。

- [ ] **Step 4: 运行测试确认 PASS**

```bash
rtk test ./build/bcos-evm/test/L1BlockPredeployTest -- --run_test=setter_unpacks_isthmus_fixture_into_slots
```

Expected: PASS。

---

### Task 4: View getter dispatch + 测试

**Files:**
- Modify: `bcos-evm/opstack/l1/L1BlockPredeploy.cpp`
- Modify: `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`

**Interfaces — Consumes:** `L1BlockSelectors.h`、Task 2 unpack 函数、`successWithU256`。

- [ ] **Step 1: 在测试中增加 `callGetterOutputHex` helper**

```cpp
bytes callGetterOutput(state::State& state, uint32_t selector)
{
    auto payload = selectorInput(selector);
    auto result = L1BlockPredeploy::dispatch(state, makeCall(payload, OP_DEPOSITOR_ACCOUNT));
    BOOST_REQUIRE(result.has_value());
    BOOST_REQUIRE_EQUAL(result->status_code, EVMC_SUCCESS);
    bytes out(result->output_data, result->output_data + result->output_size);
    if (result->release) result->release(&result.value());
    return out;
}
```

- [ ] **Step 2: 扩展 `getters_return_slot_values_after_setter`**

对 spec §4.3.2 **每一行**（含 `l1FeeOverhead`/`l1FeeScalar`/`daFootprint` 零值）及 §5.2 alias，比对 **完整 output 字节**（`BOOST_CHECK_EQUAL_COLLECTIONS` 与 hex 转 `bytes`）。

先运行应 **FAIL**（缺 `basefee()` 等 selector）。

- [ ] **Step 3: 实现 view getter dispatch**

推荐结构：

```cpp
enum class GetterKind { SlotU256, SlotPackedU64, SlotPackedU32, SlotPackedU16, SlotBytes32 };

struct GetterEntry { uint32_t selector; GetterKind kind; u256 slot; };

// 表驱动 switch 或 std::array 遍历
case l1block::kBasefee:
case l1block::kL1BaseFee:
    return successWithU256(msg.gas, state::fromEvmC(state.get_storage(..., L1_BASE_FEE_SLOT)));
case l1block::kNumber:
    return successWithU256(msg.gas, unpackNumber(state.get_storage(..., L1_NUMBER_TIMESTAMP_SLOT)));
// ... §5.1 全部 + §5.2 alias 共用路径
```

`hash()` / `batcherHash()`：`state::fromEvmC(slot)` 整 word 返回（同 bytes32 ABI）。

- [ ] **Step 4: 运行测试**

```bash
rtk test ./build/bcos-evm/test/L1BlockPredeployTest -- --run_test=getters_return_slot_values_after_setter
```

Expected: PASS。

---

### Task 5: Pure getter + string/tuple 编码 + 测试

**Files:**
- Modify: `bcos-evm/opstack/l1/L1BlockPredeploy.cpp`
- Modify: `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`

- [ ] **Step 1: 新增测试 `pure_getters_match_l1block_constants`**

对 spec §5.3.1 **全表** hex 做 `BOOST_CHECK_EQUAL_COLLECTIONS`（含 `DEPOSITOR_ACCOUNT`、`isCustomGasToken`）。

- [ ] **Step 2: 运行确认 FAIL**

```bash
rtk test ./build/bcos-evm/test/L1BlockPredeployTest -- --run_test=pure_getters_match_l1block_constants
```

- [ ] **Step 3: 实现 pure getter handlers**

```cpp
case l1block::kDepositorAccount:
    return successWithBytes(msg.gas, encodeAbiAddress(OP_DEPOSITOR_ACCOUNT));
case l1block::kIsCustomGasToken:
    return successWithU256(msg.gas, 0);
case l1block::kGasPayingToken:
    return makeResult(EVMC_SUCCESS, msg.gas, encodeGasPayingToken()); // Constants.ETHER + 18
case l1block::kGasPayingTokenName:
    return makeResult(EVMC_SUCCESS, msg.gas, encodeAbiString("Ether"));
case l1block::kGasPayingTokenSymbol:
    return makeResult(EVMC_SUCCESS, msg.gas, encodeAbiString("ETH"));
case l1block::kVersion:
    return makeResult(EVMC_SUCCESS, msg.gas, encodeAbiString("1.9.0"));
```

`Constants.ETHER` = `0xEeeeeEeeeEeEeeEeEeEeeEEEeeeeEeeeeeeeEEeE`（20 字节写入 address word）。

- [ ] **Step 4: 运行 pure getter 测试 PASS**

---

### Task 6: `isFeatureEnabled` + 测试

**Files:**
- Modify: `bcos-evm/opstack/l1/L1BlockPredeploy.cpp`
- Modify: `bcos-evm/test/opstack/L1BlockPredeployTest.cpp`

- [ ] **Step 1: 新增 `isFeatureEnabled_returns_false_by_default`**

```cpp
bytes input = {0x47, 0xaf, 0x26, 0x7b}; // kIsFeatureEnabled
evmc_bytes32 key{};
key.bytes[31] = 0x42;
input.insert(input.end(), key.bytes, key.bytes + 32);
auto out = callGetterOutput(state, /* 或直接用 dispatch */);
// 期望 output: 32 字节 0（false）
```

- [ ] **Step 2: 实现 dispatch**

```cpp
case l1block::kIsFeatureEnabled:
    if (input.size() < 36) return makeResult(EVMC_REVERT, msg.gas);
    evmc_bytes32 key{};
    std::memcpy(key.bytes, input.data() + 4, 32);
    return successWithU256(msg.gas, readFeatureEnabled(state, key) ? 1 : 0);
```

- [ ] **Step 3: 全量 Predeploy 测试**

```bash
rtk test ./build/bcos-evm/test/L1BlockPredeployTest
```

Expected: 全部 PASS。

---

### Task 7: `L1BlockGetterTest` E2E smoke

**Files:**
- Modify: `bcos-evm/test/opstack/L1BlockGetterTest.cpp`

- [ ] **Step 1: 保留现有 `l1BaseFee()` smoke（可选）**

- [ ] **Step 2: 新增 E2E `basefee()` + `number()`**

```cpp
BOOST_AUTO_TEST_CASE(op_host_extension_dispatches_basefee_getter)
{
    // 预置 slot 1 = 0x0123456789abcdef（与 fixture 一致）
    bytes input = {0x5c, 0xf2, 0x49, 0x69};  // basefee()
    // ... opStackExecuteViaHost ...
    BOOST_CHECK_EQUAL(state::fromEvmC(raw), u256(0x0123456789abcdefULL));
}

BOOST_AUTO_TEST_CASE(op_host_extension_dispatches_number_getter)
{
    // 预置 slot 0 number 部分 [24:32) = fixture
    bytes input = {0x83, 0x81, 0xf5, 0x8a};  // number()
    // 期望 u256 右对齐 0x2122232425262728
}
```

- [ ] **Step 3: 运行**

```bash
rtk test ./build/bcos-evm/test/L1BlockGetterTest
```

Expected: PASS。

---

### Task 8: 文档与 Done 清单

**Files:**
- Create or modify: `bcos-evm/docs/audits/2026-06-20-opstack-isthmus-work-list.md`
- Modify: `bcos-evm/docs/audits/_work/task5-l1block-attributes.md`
- Modify: `bcos-evm/capability-matrix.md`（L1Block 行）

- [ ] **Step 1: work-list OP-14 `[x]`**

若文件不存在，创建最小表：ID `OP-14`、标题、验收测试 `L1BlockPredeployTest` + `L1BlockGetterTest`。

- [ ] **Step 2: task5 笔记**

D5-1/2/3 标闭合；注明 GPO / proxyAdmin / setFeature 仍 gap。

- [ ] **Step 3: capability-matrix**

L1Block 行：call-surface parity ✅；known gap：GPO `0x4200…000F`、`proxyAdmin`、`setFeature`。

- [ ] **Step 4: 最终验收**

```bash
ctest -R 'L1BlockPredeploy|L1BlockGetter' --output-on-failure
```

Expected: 全部 PASS。

---

## Spec 覆盖自检

| Spec 章节 | Task |
|-----------|------|
| §4 metadata storage 写入 | Task 3 |
| §5.1 view getters | Task 4 |
| §5.2 legacy alias | Task 4 |
| §5.3 pure getters | Task 5 |
| §5.4 mapping | Task 6 |
| §4.3 / §5.3.1 金标准测试 | Task 3–6 |
| §9 E2E smoke | Task 7 |
| §9.2 Done 文档 | Task 8 |
| GPO out of scope | Global Constraints |
| 计费不变 | 无 task（刻意不改 OpStackFee） |

## 执行选项

Plan 已保存至 `docs/superpowers/plans/2026-06-20-opstack-l1block-full-parity.md`。

1. **Subagent-Driven（推荐）** — 每 Task 独立 subagent + 阶段审查  
2. **Inline Execution** — 本 session 用 executing-plans 按 Task 批量执行  

请选择执行方式。
