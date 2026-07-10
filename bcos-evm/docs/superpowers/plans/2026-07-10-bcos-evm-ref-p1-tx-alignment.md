# bcos-evm-ref P1 交易执行对齐 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `bcos-evm-ref/opstack/` 补齐 5 条 P1 对齐线——pre-Isthmus fork+历史 L1 公式、L1Block 现场解 fee、vault stub、EIP-7702 真实 ecrecover、EIP-7623 floor 验收——使 ref 交易执行更贴近 op-geth。

**Architecture:** 纯 ref-only 增量；不改生产 `bcos-evm/opstack/`。fork 感知通过 `OpForkConfig` 新增字段驱动；`computeL1Cost` 按 fork 分 Ecotone/Fjord+ 两条公式；`FromState` 重载在内部 `loadOpFeeParams` 后转调既有注入式 API；7702 用 evmone `evmmax::secp256k1::ecrecover` + `rlp::encode_tuple` 恢复 authority。

**Tech Stack:** C++20、evmone `test/state`、evmone_precompiles/secp256k1、intx、GoogleTest、CMake（vcpkg）。

**Spec:** `bcos-evm/docs/superpowers/specs/2026-07-10-bcos-evm-ref-p1-tx-alignment-design.md`

## Global Constraints

- 仅改 `bcos-evm-ref/`；**禁止** `#include <bcos-evm/...>`。
- 命名 camelCase 函数（`opValidate`/`opTransition` 风格），常量 `k` 前缀，类型/结构体大驼峰。
- 不解除 E-b park；不宣称 op-geth 块级/生产等价。
- 继承 N-1（L1 vault 无条件 touch）与 G-1（用户 tx 空 envelope → `invalid_argument`）。
- pre-Isthmus config `precompiles = nullptr`；**严禁**复用 `isthmusPrecompileOverrides()`。
- 测试期望值不得手改凑绿；金值须注明来源。
- 构建目标：`bcos-evm-ref-opstack-tests`；构建目录用现有 `bcos-evm-ref/build`。
- 每个 Task 结束跑一次 `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests` 并执行相关用例。

---

## File Structure

| 文件 | 责任 | 动作 |
|------|------|------|
| `include/bcos-evm-ref/opstack/OpForkSchedule.h` | `OpForkConfig` 加 `has_ecotone_l1_formula`；声明 4 个 pre-Isthmus config | 修改 |
| `opstack/OpForkSchedule.cpp` | 定义 ecotone/fjord/granite/holocene config；旧 config 补新字段 | 修改 |
| `include/bcos-evm-ref/opstack/RollupCost.h` | 声明 `bedrockCalldataGasUsed`；`computeL1Cost` 加 `cfg` | 修改 |
| `opstack/RollupCost.cpp` | Ecotone/Fjord+ 分叉 L1 公式；bedrock 计数 | 修改 |
| `include/bcos-evm-ref/opstack/OpFeeParams.h` | 声明 `loadOpFeeParams` | 修改 |
| `opstack/OpFeeParams.cpp` | 从 `StateView` 读 slot 1/3/7/8 → unpack | 修改 |
| `include/bcos-evm-ref/opstack/OpValidate.h` | 声明 `opValidateFromState` | 修改 |
| `opstack/OpValidate.cpp` | `computeL1Cost` 传 cfg；`opValidateFromState` 薄封装 | 修改 |
| `include/bcos-evm-ref/opstack/OpTransition.h` | 声明 `opTransitionFromState` | 修改 |
| `opstack/OpTransition.cpp` | 7702 ecrecover；`computeL1Cost` 传 cfg；`opTransitionFromState` | 修改 |
| `opstack/OpPredeploys.cpp` / `.h` | 4 vault 写最小非空 code+code_hash；更新注释 | 修改 |
| `test/opstack/OpForkScheduleTest.cpp` | pre-Isthmus config 字段/precompiles 钉死 | 修改 |
| `test/opstack/RollupCostTest.cpp` | bedrock 无 +68；Ecotone≠Fjord | 修改 |
| `test/opstack/OpFeeParamsTest.cpp` | `loadOpFeeParams` ≡ 手读 | 修改 |
| `test/opstack/OpBlockHarnessTest.cpp` | 改用 `loadOpFeeParams`；FromState≡注入 | 修改 |
| `test/opstack/OpPredeploysTest.cpp` | vault 非空 code | 修改 |
| `test/opstack/OpZeroDiffTest.cpp` | fee=0 时 L1 vault 不入 deleted | 修改 |
| `test/opstack/Op7702Test.cpp` | 7702 端到端矩阵（≥3 无预置 signer） | 新建 |
| `test/opstack/OpFloorGasTest.cpp` | 7623 单测 + 金值 fixture | 新建 |
| `test/CMakeLists.txt` | 挂新测试源 | 修改 |
| `README.md` / `docs/vector-schema.md` | 里程碑标注 | 修改 |

**依赖顺序：** T1 → T2 → T3 →（T4 / T5 / T6 串行或谨慎并行）→ T7。
- T2 **接口依赖** T1 的 `has_ecotone_l1_formula`（真实消费）。
- T3 排 T2 之后属**编辑冲突规避**（二者同改 `OpValidate.cpp`/`OpTransition.cpp`），非接口消费——`loadOpFeeParams`/`*FromState` 只是薄封装既有 `opValidate`/`opTransition`，`opValidate` 的 `cfg` 形参本就存在。
- T5 同改 `OpTransition.cpp`，须排在 T3 **之后**串行；T4（`OpPredeploys.*`）、T6（纯新增测试）不碰 `OpTransition.cpp`，可与 T5 并行。

---

## Task 1: OpForkSchedule — pre-Isthmus configs

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpForkSchedule.h`
- Modify: `bcos-evm-ref/opstack/OpForkSchedule.cpp`
- Test: `bcos-evm-ref/test/opstack/OpForkScheduleTest.cpp`

**Interfaces:**
- Consumes: 现有 `OpFork`（已含 `Ecotone/Fjord/Granite/Holocene/Isthmus/Jovian/Karst`）、`isthmusPrecompileOverrides()`。
- Produces:
  - `OpForkConfig` 新增 `bool has_ecotone_l1_formula;`
  - `const OpForkConfig& ecotoneConfig() noexcept;`（同 `fjordConfig/graniteConfig/holoceneConfig`）
  - 语义：Ecotone `has_ecotone_l1_formula=true`，其余全 false；四者 `precompiles=nullptr`、`rev=EVMC_CANCUN`、`has_operator_fee=false`。

- [ ] **Step 1: 写失败测试**

在 `OpForkScheduleTest.cpp` 追加：

```cpp
TEST(OpForkSchedule, PreIsthmusConfigsPinned)
{
    for (const auto* cfg : {&ecotoneConfig(), &fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        EXPECT_EQ(cfg->rev, EVMC_CANCUN);
        EXPECT_EQ(cfg->precompiles, nullptr);       // pre-Isthmus 不复用 Isthmus 表
        EXPECT_TRUE(cfg->disable_prague_requests);
        EXPECT_FALSE(cfg->has_operator_fee);
        EXPECT_FALSE(cfg->has_jovian_operator_formula);
        EXPECT_FALSE(cfg->has_da_footprint);
    }
    EXPECT_EQ(ecotoneConfig().fork, OpFork::Ecotone);
    EXPECT_EQ(fjordConfig().fork, OpFork::Fjord);
    EXPECT_EQ(graniteConfig().fork, OpFork::Granite);
    EXPECT_EQ(holoceneConfig().fork, OpFork::Holocene);
    EXPECT_TRUE(ecotoneConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(fjordConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(graniteConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(holoceneConfig().has_ecotone_l1_formula);
}

TEST(OpForkSchedule, IsthmusPlusDisableEcotoneL1Formula)
{
    EXPECT_FALSE(isthmusConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(jovianConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(karstConfig().has_ecotone_l1_formula);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests`
Expected: 编译失败（`has_ecotone_l1_formula` / `ecotoneConfig` 未定义）。

- [ ] **Step 3: 改头文件**

`OpForkSchedule.h` 中 `OpForkConfig` 加字段并声明 config：

```cpp
struct OpForkConfig
{
    OpFork fork;
    evmc_revision rev;
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
    bool has_jovian_operator_formula;
    bool has_da_footprint;
    bool has_ecotone_l1_formula;  // true → Ecotone calldataGas L1；false → Fjord+ FastLZ
};

const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;
```

- [ ] **Step 4: 改 cpp**

`OpForkSchedule.cpp`：现有三个 config 各加 `.has_ecotone_l1_formula = false,`；新增四个：

```cpp
const OpForkConfig& ecotoneConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Ecotone,
        .rev = EVMC_CANCUN,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .has_ecotone_l1_formula = true,
    };
    return cfg;
}

const OpForkConfig& fjordConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Fjord,
        .rev = EVMC_CANCUN,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& graniteConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = fjordConfig();
        c.fork = OpFork::Granite;
        return c;
    }();
    return cfg;
}

const OpForkConfig& holoceneConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = fjordConfig();
        c.fork = OpFork::Holocene;
        return c;
    }();
    return cfg;
}
```

> 注：`graniteConfig`/`holoceneConfig` 以拷贝 `fjordConfig()` + 改 `fork` 实现「≡ Fjord」，避免字段重复漂移。

- [ ] **Step 5: 跑测试确认通过**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R OpForkSchedule --output-on-failure`
Expected: PASS（含既有 Jovian/Karst 用例）。

- [ ] **Step 6: 提交**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/OpForkSchedule.h bcos-evm-ref/opstack/OpForkSchedule.cpp bcos-evm-ref/test/opstack/OpForkScheduleTest.cpp
git commit -m "feat(evm-ref): add pre-Isthmus op fork configs (ecotone/fjord/granite/holocene)"
```

---

## Task 2: RollupCost — fork 感知 L1 + bedrock 计数

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/RollupCost.h`
- Modify: `bcos-evm-ref/opstack/RollupCost.cpp`
- Modify: `bcos-evm-ref/opstack/OpValidate.cpp`（调用点传 cfg）
- Modify: `bcos-evm-ref/opstack/OpTransition.cpp`（若有 `computeL1Cost` 调用点；当前用 `props.l1_cost`，通常无需改，但须确认）
- Test: `bcos-evm-ref/test/opstack/RollupCostTest.cpp`

**Interfaces:**
- Consumes: `OpForkConfig::has_ecotone_l1_formula`（T1）。
- Produces:
  - `uint64_t bedrockCalldataGasUsed(evmc::bytes_view env) noexcept;` = `zeroes*4 + nonZeroes*16`（无 +68）。
  - `intx::uint256 computeL1Cost(const OpFeeParams&, evmc::bytes_view, const OpForkConfig&) noexcept;`（**签名变更**：加 `cfg`）。

- [ ] **Step 1: 写失败测试**

`RollupCostTest.cpp` 追加（并把现有 `computeL1Cost(fee, env)` 调用改成传 `fjordConfig()`）：

```cpp
TEST(RollupCost, BedrockCalldataGasUsedNoPlus68)
{
    // 3 个零字节 + 2 个非零字节 = 3*4 + 2*16 = 44；确认没有 pre-Regolith 的 +68。
    const std::vector<uint8_t> env{0x00, 0x00, 0x00, 0x11, 0x22};
    EXPECT_EQ(bedrockCalldataGasUsed({env.data(), env.size()}), 44u);
    EXPECT_NE(bedrockCalldataGasUsed({env.data(), env.size()}), 44u + 68u);
}

TEST(RollupCost, EcotoneL1DiffersFromFjordSameEnvelope)
{
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256};
    std::vector<uint8_t> env(200, 0x11);
    const auto ecotone = computeL1Cost(fee, {env.data(), env.size()}, ecotoneConfig());
    const auto fjord = computeL1Cost(fee, {env.data(), env.size()}, fjordConfig());
    EXPECT_NE(ecotone, fjord);

    // Ecotone 公式钉死：calldataGas * (l1BaseFee*16*baseScalar + blobBaseFee*blobScalar) / 16e6
    const auto calldataGas = intx::uint256{bedrockCalldataGasUsed({env.data(), env.size()})};
    const auto expected =
        calldataGas * (fee.l1_base_fee * 16 * intx::uint256{fee.base_fee_scalar} +
                          fee.blob_base_fee * intx::uint256{fee.blob_base_fee_scalar}) /
        intx::uint256{16'000'000};
    EXPECT_EQ(ecotone, expected);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests`
Expected: 编译失败（`bedrockCalldataGasUsed` 未声明 / `computeL1Cost` 参数不匹配）。

- [ ] **Step 3: 改 RollupCost.h**

```cpp
/// Ecotone L1 calldata gas：zeroes*4 + nonZeroes*16（无 pre-Regolith +68）。
uint64_t bedrockCalldataGasUsed(evmc::bytes_view signedTxEnvelope) noexcept;

/// L1 data fee。Ecotone(has_ecotone_l1_formula) 走 calldataGas 公式；Fjord+ 走 FastLZ。
/// 空 envelope 返 0；deposit 恒零由调用方保证。
intx::uint256 computeL1Cost(const OpFeeParams& params, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg) noexcept;
```

删除旧的两参 `computeL1Cost` 声明。

- [ ] **Step 4: 改 RollupCost.cpp**

新增常量与函数，改造 `computeL1Cost`：

```cpp
constexpr int64_t kZeroByteCost = 4;
// kNonzeroByteCost = 16 已存在

uint64_t bedrockCalldataGasUsed(evmc::bytes_view env) noexcept
{
    uint64_t zeroes = 0;
    uint64_t nonZeroes = 0;
    for (const auto b : env)
        (b == 0 ? zeroes : nonZeroes)++;
    return zeroes * static_cast<uint64_t>(kZeroByteCost) +
           nonZeroes * static_cast<uint64_t>(kNonzeroByteCost);
}

intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept
{
    if (signedTxEnvelope.empty())
        return intx::uint256{0};

    const auto calldataPerByte = params.l1_base_fee * intx::uint256{params.base_fee_scalar} *
                                 intx::uint256{kNonzeroByteCost};
    const auto blobPerByte = params.blob_base_fee * intx::uint256{params.blob_base_fee_scalar};

    if (cfg.has_ecotone_l1_formula)
    {
        // op-geth newL1CostFuncEcotone:
        //   calldataGas*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/16e6
        const auto calldataGas = intx::uint256{bedrockCalldataGasUsed(signedTxEnvelope)};
        return calldataGas * (calldataPerByte + blobPerByte) / intx::uint256{16'000'000};
    }

    // Fjord+（现实现）:
    //   estimatedDaSizeScaled(flz)*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/1e12
    const auto scaled = estimatedDaSizeScaled(flzCompressLen(signedTxEnvelope));
    return scaled * (calldataPerByte + blobPerByte) / intx::uint256{kFjordDivisor};
}
```

- [ ] **Step 5: 更新调用点**

`OpValidate.cpp` 第 21 行改为：

```cpp
    const auto l1Cost = computeL1Cost(fee, signedTxEnvelope, cfg);
```

检查 `OpTransition.cpp` 是否直接调用 `computeL1Cost`（当前用 `props.l1_cost`，预期无需改）。用 grep 确认：

Run: `rg -n "computeL1Cost" bcos-evm-ref/`
Expected: 仅 `OpValidate.cpp` 与 `RollupCost.*`、测试。

- [ ] **Step 6: 跑测试确认通过**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R "RollupCost|OpValidate" --output-on-failure`
Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/RollupCost.h bcos-evm-ref/opstack/RollupCost.cpp bcos-evm-ref/opstack/OpValidate.cpp bcos-evm-ref/test/opstack/RollupCostTest.cpp
git commit -m "feat(evm-ref): fork-aware L1 cost (ecotone calldataGas vs fjord fastlz)"
```

---

## Task 3: loadOpFeeParams + FromState 重载

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpFeeParams.h`
- Modify: `bcos-evm-ref/opstack/OpFeeParams.cpp`
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpValidate.h` / `opstack/OpValidate.cpp`
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpTransition.h` / `opstack/OpTransition.cpp`
- Test: `bcos-evm-ref/test/opstack/OpFeeParamsTest.cpp`、`OpBlockHarnessTest.cpp`

**Interfaces:**
- Consumes: `unpackOpFeeParams`（已存在，读 slot1/3/7/8）、`OP_L1_BLOCK`、`StateView::get_storage`。
- Produces:
  - `OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept;`
  - `std::variant<OpTxProperties, std::error_code> opValidateFromState(view, block, tx, envelope, cfg, blockGasLeft);`
  - `OpTxReceipt opTransitionFromState(view, block, hashes, tx, cfg, vm, props, chainId, envelope);`
  - **配对约束**：`*FromState` 必须成对；不得与注入式混用。

- [ ] **Step 1: 写失败测试**

`OpFeeParamsTest.cpp` 追加：

```cpp
TEST(OpFeeParams, LoadFromStateEqualsManualUnpack)
{
    using namespace evmone;
    // TestState : public state::StateView，可直接作 loadOpFeeParams 入参。
    // TestAccount::storage 是 std::map<bytes32, bytes32>（裸 bytes32，无 .current）。
    test::TestState ts;
    auto key = [](uint8_t s) { evmc::bytes32 k{}; k.bytes[31] = s; return k; };
    auto low8 = [](uint64_t v) { evmc::bytes32 w{}; for (int i = 0; i < 8; ++i)
        w.bytes[31 - i] = static_cast<uint8_t>(v >> (8 * i)); return w; };
    ts[OP_L1_BLOCK].storage[key(1)] = low8(1000000000);
    ts[OP_L1_BLOCK].storage[key(7)] = low8(10000000);

    const auto loaded = loadOpFeeParams(ts);  // ts 即 StateView
    const auto manual = unpackOpFeeParams(ts.get_storage(OP_L1_BLOCK, key(1)),
        ts.get_storage(OP_L1_BLOCK, key(3)), ts.get_storage(OP_L1_BLOCK, key(7)),
        ts.get_storage(OP_L1_BLOCK, key(8)));
    EXPECT_EQ(loaded.l1_base_fee, manual.l1_base_fee);
    EXPECT_EQ(loaded.blob_base_fee, manual.blob_base_fee);
    EXPECT_EQ(loaded.l1_base_fee, 1000000000_u256);
}
```

> 已核实（vcpkg `test/utils/test_state.hpp`）：`TestState : public state::StateView`，其 `get_storage(addr,key) const -> bytes32`；`TestAccount{uint64_t nonce; uint256 balance; std::map<bytes32,bytes32> storage; bytes code;}`（**无 `code_hash` 字段**）。注意 `state::State` 并**不**继承 `StateView`，故测试直接用 `TestState`，勿包 `state::State`。

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests`
Expected: 编译失败（`loadOpFeeParams` 未声明）。

- [ ] **Step 3: 实现 loadOpFeeParams**

`OpFeeParams.h`：

```cpp
#include <test/state/state.hpp>
// ...
/// 从 OP_L1_BLOCK 的 slot 1/3/7/8 读取并 unpack（缺槽视为零字）。
OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept;
```

`OpFeeParams.cpp`：

```cpp
#include <bcos-evm-ref/opstack/OpPredeploys.h>

OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept
{
    auto slot = [](uint8_t s) { evmc::bytes32 k{}; k.bytes[31] = s; return k; };
    return unpackOpFeeParams(view.get_storage(OP_L1_BLOCK, slot(1)),
        view.get_storage(OP_L1_BLOCK, slot(3)), view.get_storage(OP_L1_BLOCK, slot(7)),
        view.get_storage(OP_L1_BLOCK, slot(8)));
}
```

> 确认 `StateView::get_storage(address, bytes32)` 签名；evmone `StateView` 提供该接口。

- [ ] **Step 4: 实现 FromState 重载**

`OpValidate.h` 声明 + `OpValidate.cpp` 实现：

```cpp
std::variant<OpTxProperties, std::error_code> opValidateFromState(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, int64_t blockGasLeft)
{
    return opValidate(view, block, tx, signedTxEnvelope, cfg, loadOpFeeParams(view), blockGasLeft);
}
```

`OpTransition.h` 声明 + `OpTransition.cpp` 实现（头注释写明配对约束）：

```cpp
OpTxReceipt opTransitionFromState(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope)
{
    return opTransition(
        view, block, hashes, tx, cfg, vm, props, loadOpFeeParams(view), chainId, signedTxEnvelope);
}
```

- [ ] **Step 5: harness 改用 loadOpFeeParams + FromState≡注入 断言**

`OpBlockHarnessTest.cpp` 中手写四槽 `unpackOpFeeParams(...)` 处改为：

```cpp
    const auto fee = loadOpFeeParams(ts);
    // 对照：与手读四槽一致（保留一次断言）
    const auto manual = unpackOpFeeParams(ts.get_storage(OP_L1_BLOCK, slotKey(1)),
        ts.get_storage(OP_L1_BLOCK, slotKey(3)), ts.get_storage(OP_L1_BLOCK, slotKey(7)),
        ts.get_storage(OP_L1_BLOCK, slotKey(8)));
    EXPECT_EQ(fee.l1_base_fee, manual.l1_base_fee);
```

再补一条 FromState≡注入 的断言（同 tx 两路径 receipt.gas_used / meta 相等）。

- [ ] **Step 6: 跑测试确认通过**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R "OpFeeParams|OpBlockHarness" --output-on-failure`
Expected: PASS。

- [ ] **Step 7: 提交**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/OpFeeParams.h bcos-evm-ref/opstack/OpFeeParams.cpp bcos-evm-ref/include/bcos-evm-ref/opstack/OpValidate.h bcos-evm-ref/opstack/OpValidate.cpp bcos-evm-ref/include/bcos-evm-ref/opstack/OpTransition.h bcos-evm-ref/opstack/OpTransition.cpp bcos-evm-ref/test/opstack/OpFeeParamsTest.cpp bcos-evm-ref/test/opstack/OpBlockHarnessTest.cpp
git commit -m "feat(evm-ref): loadOpFeeParams + FromState validate/transition overloads"
```

---

## Task 4: Vault stub code

**Files:**
- Modify: `bcos-evm-ref/opstack/OpPredeploys.cpp`
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpPredeploys.h`（更新注释）
- Test: `bcos-evm-ref/test/opstack/OpPredeploysTest.cpp`、`OpZeroDiffTest.cpp`

**Interfaces:**
- Consumes: `seedOpPredeploys(TestState&)`、四个 vault 地址、`evmone::keccak256`。
- Produces: 四个 vault 账户带非空 `code`（1 字节 `0x00`）与对应 `code_hash`；`OP_L1_BLOCK`/`OP_GAS_PRICE_ORACLE` 的 code 不被写。

- [ ] **Step 1: 写失败测试**

`OpPredeploysTest.cpp` 追加：

```cpp
TEST(OpPredeploys, VaultsHaveNonEmptyCode)
{
    evmone::test::TestState ts;
    seedOpPredeploys(ts);
    for (const auto& v : {OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT,
             OP_SEQUENCER_FEE_VAULT})
    {
        EXPECT_FALSE(ts[v].code.empty()) << "vault should have stub code";
    }
    // L1Block / oracle 的 code 不由本函数写入（harness setter 自管）。
    EXPECT_TRUE(ts[OP_L1_BLOCK].code.empty());
    EXPECT_TRUE(ts[OP_GAS_PRICE_ORACLE].code.empty());
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R OpPredeploys --output-on-failure`
Expected: FAIL（vault code 为空）。

- [ ] **Step 3: 实现 stub**

`OpPredeploys.cpp`：

```cpp
void seedOpPredeploys(evmone::test::TestState& state)
{
    for (const auto& addr : {OP_L1_BLOCK, OP_GAS_PRICE_ORACLE, OP_SEQUENCER_FEE_VAULT,
             OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT})
    {
        state[addr];  // 保证账户存在
    }
    // 四个 fee vault：最小非空 runtime code（1 字节 STOP），使其在零值差分下不被当空账户删除。
    for (const auto& v : {OP_SEQUENCER_FEE_VAULT, OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT,
             OP_OPERATOR_FEE_VAULT})
    {
        state[v].code = evmone::bytes{0x00};
    }
}
```

> 已核实：`TestAccount` 仅有 `{nonce, balance, storage, code}`，**无 `code_hash` 字段**（code_hash 由 `build_diff`/状态层据 `code` 计算）。故只写 `.code` 即可，无需手更 hash。`.code` 类型为 `evmone::bytes`（同 `OpBlockHarnessTest.cpp` 里 `ts[OP_L1_BLOCK].code = ...` 用法）。无需 include hash_utils。

- [ ] **Step 4: 收紧 ZeroDiff 断言**

`OpZeroDiffTest.cpp`：既然 vault 现在非空，`fee=0` 下 `OP_L1_FEE_VAULT` 不应进入 `deleted_accounts`。把先前放宽的 `nonVaultDeleted` 排除项收紧（至少移除对 `OP_L1_FEE_VAULT` 的豁免），并加断言：

```cpp
// fee=0 下四个 vault 因已有 stub code 不再被判为空账户删除
for (const auto& v : {OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT,
         OP_SEQUENCER_FEE_VAULT})
{
    EXPECT_EQ(std::count(opReceipt.state_diff.deleted_accounts.begin(),
                  opReceipt.state_diff.deleted_accounts.end(), v),
        0) << "vault should not be deleted";
}
```

> 若 harness 造世未调用 `seedOpPredeploys`，需在该测试的 state 准备处补调，确保 vault 有 stub code。

- [ ] **Step 5: 跑测试确认通过**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R "OpPredeploys|OpZeroDiff" --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 更新头注释 + 提交**

`OpPredeploys.h` 把「真实 bytecode 延后 M5」改为反映 vault 已有 stub、L1Block/oracle code 仍由 harness 自管。

```bash
git add bcos-evm-ref/opstack/OpPredeploys.cpp bcos-evm-ref/include/bcos-evm-ref/opstack/OpPredeploys.h bcos-evm-ref/test/opstack/OpPredeploysTest.cpp bcos-evm-ref/test/opstack/OpZeroDiffTest.cpp
git commit -m "feat(evm-ref): seed fee vaults with non-empty stub code; tighten zero-diff"
```

---

## Task 5: EIP-7702 真实 ecrecover

**Files:**
- Modify: `bcos-evm-ref/opstack/OpTransition.cpp`（`process_authorization_list`）
- Test: `bcos-evm-ref/test/opstack/Op7702Test.cpp`（新建）
- Create: `bcos-evm-ref/test/opstack/scripts/gen_7702_vectors.py`（金值生成）
- Modify: `bcos-evm-ref/test/CMakeLists.txt`（新增源在既有 `bcos-evm-ref-opstack-tests` 目标下）

**Interfaces:**
- Consumes: `Authorization{chain_id, addr, nonce, signer(optional), r, s, v}`、`evmmax::secp256k1::ecrecover(span<hash,32>, span<r,32>, span<s,32>, bool parity)`、`evmone::rlp::encode_tuple`、`evmone::keccak256`、`intx::be::store`。
- Produces: `process_authorization_list` 在 `!auth.signer` 时用 `r/s/v` 恢复 authority；恢复失败 `continue`；其余逻辑不变。

- [ ] **Step 0: 生成 7702 签名金值（一次性脚本）**

新建 `bcos-evm-ref/test/opstack/scripts/gen_7702_vectors.py`，对给定私钥与 `(chain_id, addr, nonce)` 计算 EIP-7702 签名并打印 `authority / r / s / v`。用 `eth_account`（`pip install eth-account>=0.11`）：

```python
# gen_7702_vectors.py — 生成 EIP-7702 授权签名金值（写入测试注释来源）
from eth_account import Account
from eth_account.messages import encode_typed_data  # noqa: F401
from eth_utils import keccak
import rlp

def auth_hash(chain_id: int, address: bytes, nonce: int) -> bytes:
    payload = rlp.encode([chain_id, address, nonce])
    return keccak(b"\x05" + payload)

def sign(priv_hex: str, chain_id: int, address: bytes, nonce: int):
    acct = Account.from_key(priv_hex)
    h = auth_hash(chain_id, address, nonce)
    sig = Account._sign_hash(h, acct.key)  # 返回 r,s,v(=y_parity+27 or 0/1)
    yparity = sig.v - 27 if sig.v in (27, 28) else sig.v
    print(f"authority = {acct.address}")
    print(f"chain_id={chain_id} addr=0x{address.hex()} nonce={nonce}")
    print(f"r = 0x{sig.r:064x}")
    print(f"s = 0x{sig.s:064x}")
    print(f"v = {yparity}")

if __name__ == "__main__":
    # 固定测试私钥（仅测试用，非真实资产）
    PRIV = "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d"
    DELEGATE = bytes.fromhex("00000000000000000000000000000000000000cc")  # 委托目标 addr
    sign(PRIV, 1, DELEGATE, 0)          # 成功用例
    sign(PRIV, 1, DELEGATE, 5)          # nonce 不匹配用例（签 nonce=5，state nonce=0）
    sign(PRIV, 999, DELEGATE, 0)        # chain_id 不匹配用例（签 chain_id=999）
```

Run: `python3 bcos-evm-ref/test/opstack/scripts/gen_7702_vectors.py`
把输出的 `authority / r / s / v` 填入 Step 1 的 `constexpr`（标注「来源：本脚本 + 私钥 0x59c6…」）。**禁止手改凑绿**。

- [ ] **Step 1: 写失败测试（5 类，≥3 条不预置 signer）**

新建 `Op7702Test.cpp`。矩阵：成功授权、坏签名(篡改 r)、nonce 不匹配、chain_id 不匹配、授权后委托调用（成功授权到有 code 的目标后主 tx 调用 authority 走委托）。前四类**不预置** `auth.signer`，强制走 ecrecover。

```cpp
#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <bcos-evm-ref/opstack/OpTransition.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kDelegate = 0x00000000000000000000000000000000000000cc_address;
// === 金值：gen_7702_vectors.py，私钥 0x59c6995e...86dae88c7a8412f4603b6b78690d ===
constexpr auto kAuthority = 0x0000000000000000000000000000000000000000_address;  // FILL: authority
constexpr auto kR_ok = 0x00_bytes32;  // FILL: chain_id=1,addr=cc,nonce=0 的 r
constexpr auto kS_ok = 0x00_bytes32;  // FILL: s
constexpr int  kV_ok = 0;             // FILL: v (0/1)

// 构造带一条 auth 的 1559 tx，执行 opTransition，返回 receipt 并把 diff 落回 ts。
OpTxReceipt runWithAuth(test::TestState& ts, evmc::VM& vm, const state::Authorization& auth)
{
    test::TestBlockHashes hashes;
    state::BlockInfo block;
    block.number = 1; block.gas_limit = 30000000; block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip7702;  // set-code tx
    tx.sender = kSender;
    tx.to = kSender;  // 简单自调用；重点在 auth 处理
    tx.gas_limit = 200000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.authorization_list = {auth};

    OpFeeParams fee{.l1_base_fee = 0_u256, .base_fee_scalar = 0, .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256, .operator_fee_scalar = 0, .operator_fee_constant = 0};
    std::vector<uint8_t> env{0x04, 0x11};  // 非空 envelope 满足 G-1
    const auto v = opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    EXPECT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    return opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, fee, /*chainId=*/1, {env.data(), env.size()});
}
}  // namespace

TEST(Op7702, RecoversAuthorityAndWritesDelegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[kDelegate] = {};
    seedOpPredeploys(ts);

    state::Authorization auth{.chain_id = 1, .addr = kDelegate, .nonce = 0,
        .signer = std::nullopt, .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok), .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    ASSERT_EQ(r.receipt.status, EVMC_SUCCESS);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);

    // authority 被写 0xef0100||kDelegate，nonce 从 0 → 1
    const auto& acc = ts.at(kAuthority);
    ASSERT_EQ(acc.code.size(), 23u);  // 3(magic) + 20(addr)
    EXPECT_EQ(acc.code[0], 0xef); EXPECT_EQ(acc.code[1], 0x01); EXPECT_EQ(acc.code[2], 0x00);
    EXPECT_EQ(acc.nonce, 1u);
}

TEST(Op7702, BadSignatureRecoverFailsNoDelegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    seedOpPredeploys(ts);
    auto badR = kR_ok; badR.bytes[31] ^= 0x01;  // 篡改 r → 恢复出错误/失败 authority

    state::Authorization auth{.chain_id = 1, .addr = kDelegate, .nonce = 0,
        .signer = std::nullopt, .r = intx::be::load<intx::uint256>(badR),
        .s = intx::be::load<intx::uint256>(kS_ok), .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    // 真 authority 未被委托、nonce 未被 bump（恢复出的地址即便非空也 nonce 不匹配 → skip）
    auto it = ts.find(kAuthority);
    if (it != ts.end())
        EXPECT_TRUE(it->second.code.empty());
}

TEST(Op7702, NonceMismatchSkips)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    seedOpPredeploys(ts);
    // 用 nonce=5 的金值，但 authority 的 state nonce=0 → 步骤6 不匹配 → skip
    // （kR_nonce5/kS_nonce5/kV_nonce5 由脚本第二组输出填入）
    // ... 断言 kAuthority 无委托 code ...
}

TEST(Op7702, ChainIdMismatchSkips)
{
    // auth.chain_id=999，tx chainId=1 → 步骤1 chain_id 不匹配 → skip（甚至不进 recover）
    // 用 signer=nullopt + 任意 r/s/v 即可（chain_id 检查在最前）
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    seedOpPredeploys(ts);
    state::Authorization auth{.chain_id = 999, .addr = kDelegate, .nonce = 0,
        .signer = std::nullopt, .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok), .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    bcos::evmref::applyStateDiff(ts, r.receipt.state_diff);
    auto it = ts.find(kAuthority);
    if (it != ts.end())
        EXPECT_TRUE(it->second.code.empty());
}
```

> 说明：`NonceMismatchSkips` 需填脚本第二组（nonce=5）金值；`ChainIdMismatchSkips` 复用第一组金值即可（chain_id 检查在最前，不依赖签名有效性）。「授权后委托调用」用例可在 `RecoversAuthorityAndWritesDelegation` 基础上：给 `kDelegate` 放一段返回固定值的 runtime code，主 tx `tx.to=kAuthority`，断言 `result` 走了委托目标逻辑（`EVMC_DELEGATED`）。至少保证前 4 类落地、≥3 类不预置 signer。

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests`
Expected: 需先把 `Op7702Test.cpp` 挂进 `test/CMakeLists.txt`（见 Task 5 Files）。编译链接后 `RecoversAuthorityAndWritesDelegation` FAIL（当前 `!signer` 直接 `continue`，authority 未写）。

- [ ] **Step 3: 实现 ecrecover**

`OpTransition.cpp` 顶部匿名命名空间加 helper：

```cpp
constexpr uint8_t kSetCodeMagic = 0x05;  // EIP-7702 authorization magic

std::optional<evmc::address> recoverAuthority(const evmone::state::Authorization& auth)
{
    // signing hash = keccak256(0x05 || rlp([chain_id, address, nonce]))
    auto msg = evmone::bytes{kSetCodeMagic} +
               evmone::rlp::encode_tuple(auth.chain_id, auth.addr, auth.nonce);
    const auto h = evmone::keccak256(msg);
    const auto r = intx::be::store<evmc::bytes32>(auth.r);
    const auto s = intx::be::store<evmc::bytes32>(auth.s);
    return evmmax::secp256k1::ecrecover(
        std::span<const uint8_t, 32>{h.bytes, 32}, std::span<const uint8_t, 32>{r.bytes, 32},
        std::span<const uint8_t, 32>{s.bytes, 32}, auth.v != 0);
}
```

`process_authorization_list` 中**整体替换**现有「v 检查 → partial-verification signer 检查(含 TODO) → s 检查 → get_or_insert」这一段（现码 L42-56），改为**先 v/s 校验、再恢复**的顺序（op-geth `ValidateSignatureValues` 在 recover 前校验 s≤N/2、v∈{0,1}）：

```cpp
        // 3. y_parity must be 0 or 1 (EIP-7702/2930).
        if (auth.v > 1)
            continue;
        // s value must be <= secp256k1n/2 (EIP-2) — 必须在 recover 之前。
        if (auth.s > SECP256K1N_OVER_2)
            continue;

        // Recover signer: 若测试预置 signer 则信任（捷径）；否则 ecrecover。
        std::optional<evmc::address> signer = auth.signer;
        if (!signer.has_value())
            signer = recoverAuthority(auth);
        if (!signer.has_value())
            continue;  // 恢复失败 → skip 该条

        // Get or create the authority account.
        auto& authority = state.get_or_insert(*signer, {.erase_if_empty = true});
```

> 关键：删掉现码 L45-48 的 partial-verification `TODO` 注释与 `if (!auth.signer.has_value()) continue;`；并把原本在 signer 检查**之后**的 s 检查（现 L51-52）上移到 recover **之前**（如上）。其后函数体内所有 `*auth.signer` 改为局部 `*signer`（如 `state.get_code(*signer)`）。

必要头（`OpTransition.cpp`）：`#include <test/utils/rlp.hpp>`（`encode_tuple` 在 `namespace evmone::rlp`）、`#include <span>`、`#include <optional>`。`encode_tuple(intx::uint256, evmc::address, uint64_t)` 三元素均有 `evmone::rlp::encode` 重载（`address` 经 bytes_view）。`auth.v` 类型为 `intx::uint256`，`auth.v != 0` 取 parity、`auth.v > 1` 判越界，均合法。

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R "Op7702|OpTransition" --output-on-failure`
Expected: PASS；失败类用例断言不写 delegation、不 bump nonce；既有 `OpTransition.*` 回归不破。

- [ ] **Step 5: 提交**

```bash
git add bcos-evm-ref/opstack/OpTransition.cpp bcos-evm-ref/test/opstack/Op7702Test.cpp bcos-evm-ref/test/opstack/scripts/gen_7702_vectors.py bcos-evm-ref/test/CMakeLists.txt
git commit -m "feat(evm-ref): real EIP-7702 ecrecover in process_authorization_list"
```

---

## Task 6: EIP-7623 floor 验收

**Files:**
- Test: `bcos-evm-ref/test/opstack/OpFloorGasTest.cpp`（新建）
- Modify: `bcos-evm-ref/test/CMakeLists.txt`
- 可能 Create: `bcos-evm-ref/test/opstack/fixtures/`（金值 fixture）

**Interfaces:**
- Consumes: `runDeposit`、`opTransition`、`props.props.min_gas_cost`（现有 floor 实现）。
- Produces: 断言 `gas_used == max(执行消耗, min_gas_cost)` 的单测 ≥2（deposit + user）+ ≥1 金值边界。
- **本 Task 不改公式实现**，仅验收。

- [ ] **Step 1: 挂 CMake + 写失败测试**

先在 `test/CMakeLists.txt` 加 `OpFloorGasTest.cpp`（同 Task 5 `Op7702Test.cpp` 的挂法）。新建 `OpFloorGasTest.cpp`，构造大零字节 calldata 使 `min_gas_cost`（7623 floor）> 纯执行消耗：

```cpp
#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <bcos-evm-ref/opstack/OpTransition.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

TEST(OpFloorGas, UserTxGasUsedRaisedToFloor)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {};  // 空账户，纯转账无执行开销
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1; block.gas_limit = 30000000; block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender; tx.to = dest;
    tx.gas_limit = 5000000; tx.max_gas_price = 1000; tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0}; tx.nonce = 0;
    tx.data = state::bytes(3000, 0x00);  // 3000 个零字节 → floor 抬升 gas_used

    OpFeeParams fee{};  // 全 0，隔离 L1/operator，聚焦 floor
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, fee, 1234, {env.data(), env.size()});
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);
    // 7623 floor 生效：gas_used 恰等于 min_gas_cost（floor），且严格大于 intrinsic
    EXPECT_EQ(txR.receipt.gas_used, props.props.min_gas_cost);
    EXPECT_GT(props.props.min_gas_cost, props.props.execution_gas_limit == 0
                                            ? 0
                                            : (tx.gas_limit - props.props.execution_gas_limit));
}

TEST(OpFloorGas, DepositGasUsedRaisedToFloor)
{
    constexpr auto depositor = OP_DEPOSITOR;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[depositor] = {.nonce = 0, .balance = 0_u256};
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1; block.gas_limit = 30000000; block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    DepositTx dep{.source_hash = 0x01_bytes32, .from = depositor, .to = depositor,
        .mint = std::nullopt, .value = intx::uint256{0}, .gas_limit = 5000000,
        .is_system_tx = false, .data = state::bytes(3000, 0x00)};

    const auto r = runDeposit(ts, block, hashes, dep, isthmusConfig(), vm, 1234);
    ASSERT_EQ(r.receipt.status, EVMC_SUCCESS);
    // deposit 同样吃 7623 floor（op-geth Isthmus 无豁免）：gas_used == floor
    // 对照：一条极小 data 的 deposit gas_used 应显著更小（证明 floor 抬升）
    DepositTx small = dep; small.data = state::bytes{};
    const auto rs = runDeposit(ts, block, hashes, small, isthmusConfig(), vm, 1234);
    EXPECT_GT(r.receipt.gas_used, rs.receipt.gas_used);
}
```

> `state::bytes` 是 `std::basic_string<uint8_t>`；`state::bytes(3000, 0x00)` 造 3000 零字节。`OpFeeParams fee{}` 聚合零初始化，隔离 L1/operator。若 `execution_gas_limit`/`min_gas_cost` 字段名与 evmone `TransactionProperties` 不符，以该头实际字段为准（现 `OpTransition.cpp` 用 `props.props.execution_gas_limit`、`props.props.min_gas_cost`）。

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R OpFloorGas --output-on-failure`
Expected: 若 floor 语义/断言不符则 FAIL；据真实行为修断言（**不改产码**）。

- [ ] **Step 3: 确认单测反映真实 floor 行为**

`gas_used == min_gas_cost` 与 deposit `大 data > 小 data` 应体现 7623 抬升。断言写错则修断言，不动 `OpTransition.cpp`/`runDeposit`。

- [ ] **Step 4: 金值 fixture（分级：先硬编码金值）**

从 `bcos-evm/test/opstack/t8n/vectors/` 或 opt8n 产物取 ≥1 条 floor 边界向量，**把输入与期望 `gas_used` 写成 C++ 常量** 直接喂 `opTransition`/`runDeposit`：

```cpp
TEST(OpFloorGas, GoldenVectorBoundary)
{
    // 来源：<vector 原始路径> 抓取于 <UTC 时间>，期望 gas_used 由 opt8n 真跑
    // 输入：tx/env/fee/cfg 常量；断言 receipt.gas_used == <金值>
}
```

> 仅当后续需要时才升级为 JSON 最小 replayer；本轮不上完整 t8n gate。期望值来源写进注释。

- [ ] **Step 5: 跑测试确认通过**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build -R OpFloorGas --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add bcos-evm-ref/test/opstack/OpFloorGasTest.cpp bcos-evm-ref/test/CMakeLists.txt bcos-evm-ref/test/opstack/fixtures 2>/dev/null; git add -A bcos-evm-ref/test
git commit -m "test(evm-ref): EIP-7623 floor acceptance (unit + golden vector)"
```

---

## Task 7: 文档

**Files:**
- Modify: `bcos-evm-ref/README.md`
- Modify: `bcos-evm-ref/docs/vector-schema.md`（如新增 fixture 字段）

**Interfaces:**
- Consumes: T1–T6 成果。
- Produces: 里程碑记录；明确仍不宣称 op-geth 块级/生产等价。

- [ ] **Step 1: 更新 README**

新增本里程碑小节：pre-Isthmus fork+历史 L1、`loadOpFeeParams`+FromState、vault stub、7702 ecrecover、7623 floor 验收。**必须写明**：pre-Isthmus `precompiles=nullptr`（precompile 集合保真非目标）；仍不宣称 op-geth 等价；N-1/G-1 继承。

- [ ] **Step 2: 更新 vector-schema（若适用）**

若金值 fixture 引入新字段/目录，补 schema 说明与来源约定。

- [ ] **Step 3: 全量回归**

Run: `cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests && ctest --test-dir bcos-evm-ref/build --output-on-failure`
Expected: 全绿。

- [ ] **Step 4: 提交**

```bash
git add bcos-evm-ref/README.md bcos-evm-ref/docs/vector-schema.md
git commit -m "docs(evm-ref): record P1 tx-alignment milestone (7702/7623/L1/vault/pre-isthmus)"
```

---

## Self-Review

**1. Spec 覆盖：**
- 目标 1（7702 端到端）→ Task 5 + Op7702Test（≥3 无预置 signer）。✅
- 目标 2（7623 floor）→ Task 6（单测 + 金值）。✅
- 目标 3（L1Block 解 fee）→ Task 3（`loadOpFeeParams` + FromState）。✅
- 目标 4（vault stub）→ Task 4。✅
- 目标 5（pre-Isthmus + 历史 L1）→ Task 1（config）+ Task 2（Ecotone/Fjord 公式 + bedrock 计数）。✅
- §2.1 `precompiles=nullptr` → Task 1 Step 3/4 + 测试钉死。✅
- §2.2 bedrock 无 +68 → Task 2 `BedrockCalldataGasUsedNoPlus68`。✅
- §2.3 FromState 配对约束 → Task 3 头注释 + 断言。✅

**2. 占位扫描：** T5/T6 测试体已从注释骨架补为完整可编译结构（构造 + 调用 + 断言）；7702 仅签名**数据常量**（r/s/v/authority）由 Step 0 脚本产出后填入（数据非逻辑，合规）。7623 全确定性、无占位。

**3. 类型一致性（已交叉核实）：**
- `computeL1Cost(fee, env, cfg)` 签名在 T2 定义、T2 Step5 更新唯一调用点 `OpValidate.cpp:21`（`OpTransition` 用 `props.l1_cost` 不调用）。✅
- `loadOpFeeParams(StateView)`、`opValidateFromState`/`opTransitionFromState` 参数顺序与既有 `opValidate(view,block,tx,envelope,cfg,fee,blockGasLeft)`/`opTransition(view,block,hashes,tx,cfg,vm,props,fee,chainId,envelope)` 对齐。✅
- `OpForkConfig.has_ecotone_l1_formula` 在 T1 定义、T2 消费。✅
- `Authorization{chain_id, addr, nonce, signer, r, s, v}`（`v` 为 `uint256`）与 evmone `test/state/transaction.hpp` 一致。✅

**4. 三重交叉检查已消解的问题：**
- ✅ [spec vs op-geth](0bfc8bde-0961-4a53-b334-5e6116d9d2c3)：6 项技术假设全 CORRECT（Ecotone/Fjord 公式、7702 hash/magic/字段序、7623 deposit 无豁免、precompile 激活点、slot 1/3/7/8）。
- ✅ [可编译性](84915b49-6b51-4c2c-9a57-536d1d0b7aa7)：`TestState : public StateView`（T3 测试直接传 `ts`，不包 `state::State`）；`TestAccount` 无 `code_hash`（T4 只写 `.code`）；storage 值为裸 `bytes32`（去掉 `.current`）；rlp 命名空间 `evmone::rlp`、头 `test/utils/rlp.hpp`。
- ✅ [覆盖度](14d52e68-2ea4-4f99-a917-40dae0f75dea)：T5 补齐 5 类矩阵 + s-check 移到 recover 之前并给出整段替换；T1 补钉 `disable_prague_requests`/`.fork`；T4 零值差分收紧到四 vault；File Structure 依赖论据更正为「编辑冲突规避」。
