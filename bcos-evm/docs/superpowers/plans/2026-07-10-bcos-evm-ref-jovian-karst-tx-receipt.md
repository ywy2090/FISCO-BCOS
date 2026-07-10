# bcos-evm-ref Jovian/Karst tx+receipt + Isthmus P0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `bcos-evm-ref/opstack` with Jovian tx execution + receipt meta, Karst config placeholder, and Isthmus P0 fixes (empty-envelope reject; no L1 vault touch when `l1_cost==0`).

**Architecture:** Minimal incremental changes on existing opstack files (spec scheme A). Fork flags drive operator formula and da-footprint receipt fields; `opTransition` returns `OpTxReceipt{receipt,meta}` built via `deriveOpReceiptMeta`. No block-header DA validation.

**Tech Stack:** C++20, gtest, evmone `3585c2cb` state Host, intx; build via `bcos-evm-ref/build` + target `bcos-evm-ref-opstack-tests`.

## Global Constraints

- Scope: **only** `bcos-evm-ref/opstack/` (+ its tests/docs); **no** `#include <bcos-evm/...>`, no production edits
- Naming: PascalCase types, camelCase free functions (README table)
- Jovian operator: `gas * scalar * 100 + constant` (op-geth `JOVIAN` / production `JOVIAN_OPERATOR_FEE_GAS_MULTIPLIER=100`)
- Jovian precompile max input: `0x08=81984`, `0x0c=288960`, `0x0e=278784`, `0x0f=156672`; `0x100` gas=3450
- `da_footprint` (receipt) = `estimatedDaSize(envelope) * da_footprint_gas_scalar` — **not** L1 blob gas / not header `BlobGasUsed` block check
- Karst: `karstConfig()` behavior flags identical to `jovianConfig()`, `fork=Karst` only
- E-b / block DA / extraData / withdrawalsRoot: **out of scope**
- Prefer `rtk` prefix for shell; commit only when task step says so

## File Structure

| Path | Role |
|------|------|
| `include/.../OpForkSchedule.h` + `opstack/OpForkSchedule.cpp` | `Jovian`/`Karst` enum; config flags; `jovianConfig`/`karstConfig` |
| `include/.../OpFeeParams.h` + `OpFeeParams.cpp` | `da_footprint_gas_scalar` unpack from slot8 `[18,20)` |
| `include/.../OpPrecompiles.h` + `OpPrecompiles.cpp` | `jovianPrecompileOverrides()` |
| `include/.../RollupCost.h` + `RollupCost.cpp` | fork-aware `computeOperatorCost`; `estimatedDaSize` |
| `include/.../OpValidate.h` + `OpValidate.cpp` | P0-2 empty envelope |
| `include/.../OpReceiptMeta.h` + `opstack/OpReceiptMeta.cpp` | `OpReceiptMeta` / `deriveOpReceiptMeta` / `OpTxReceipt` |
| `include/.../OpTransition.h` + `OpTransition.cpp` | P0-1; return `OpTxReceipt`; wire meta |
| `test/opstack/*` | TDD coverage per task |
| `README.md`, `docs/vector-schema.md` | milestone + schema notes |

---

### Task 1: OpForkSchedule — Jovian / Karst configs

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpForkSchedule.h`
- Modify: `bcos-evm-ref/opstack/OpForkSchedule.cpp`
- Modify: `bcos-evm-ref/test/opstack/OpForkScheduleTest.cpp`
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpPrecompiles.h` (declare `jovianPrecompileOverrides` forward — **or** Task 1 temporarily set `precompiles` to `&isthmusPrecompileOverrides()` and Task 4 swaps; prefer Task 1 use isthmus table then Task 4 updates jovian/karst pointers)

**Interfaces:**
- Consumes: existing `isthmusPrecompileOverrides()` (until Task 4)
- Produces: `OpFork::{Jovian,Karst}`; `OpForkConfig` fields `has_jovian_operator_formula`, `has_da_footprint`; `jovianConfig()`, `karstConfig()`; `isthmusConfig()` sets both new flags `false`

- [ ] **Step 1: Write the failing test**

Append to `OpForkScheduleTest.cpp`:

```cpp
TEST(OpForkSchedule, JovianAndKarstConfigs)
{
    const auto& j = jovianConfig();
    EXPECT_EQ(j.fork, OpFork::Jovian);
    EXPECT_EQ(j.rev, EVMC_PRAGUE);
    EXPECT_TRUE(j.has_operator_fee);
    EXPECT_TRUE(j.has_jovian_operator_formula);
    EXPECT_TRUE(j.has_da_footprint);
    EXPECT_TRUE(j.disable_prague_requests);
    EXPECT_NE(j.precompiles, nullptr);

    const auto& k = karstConfig();
    EXPECT_EQ(k.fork, OpFork::Karst);
    EXPECT_EQ(k.rev, j.rev);
    EXPECT_EQ(k.has_operator_fee, j.has_operator_fee);
    EXPECT_EQ(k.has_jovian_operator_formula, j.has_jovian_operator_formula);
    EXPECT_EQ(k.has_da_footprint, j.has_da_footprint);
    EXPECT_EQ(k.precompiles, j.precompiles);
}

TEST(OpForkSchedule, IsthmusDisablesJovianFlags)
{
    const auto& i = isthmusConfig();
    EXPECT_FALSE(i.has_jovian_operator_formula);
    EXPECT_FALSE(i.has_da_footprint);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests -j8 2>&1 | tail -40
```
Expected: compile FAIL — `Jovian` / `jovianConfig` / new fields missing.

- [ ] **Step 3: Write minimal implementation**

`OpForkSchedule.h` — replace enum/config/API with:

```cpp
#pragma once

#include <evmc/evmc.h>

namespace bcos::evmref::opstack
{
enum class OpFork
{
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
    Jovian,
    Karst,
};

struct PrecompileOverrides;

struct OpForkConfig
{
    OpFork fork;
    evmc_revision rev;
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
    bool has_jovian_operator_formula;
    bool has_da_footprint;
};

const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;
}  // namespace bcos::evmref::opstack
```

`OpForkSchedule.cpp`:

```cpp
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPrecompiles.h>

namespace bcos::evmref::opstack
{
const OpForkConfig& isthmusConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Isthmus,
        .rev = EVMC_PRAGUE,
        .precompiles = &isthmusPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
    };
    return cfg;
}

const OpForkConfig& jovianConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Jovian,
        .rev = EVMC_PRAGUE,
        .precompiles = &isthmusPrecompileOverrides(),  // Task 4 switches to jovian table
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = true,
        .has_da_footprint = true,
    };
    return cfg;
}

const OpForkConfig& karstConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Karst,
        .rev = EVMC_PRAGUE,
        .precompiles = &isthmusPrecompileOverrides(),  // Task 4: same as jovian
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = true,
        .has_da_footprint = true,
    };
    return cfg;
}
}  // namespace bcos::evmref::opstack
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests -j8 && \
  bcos-evm-ref/build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpForkSchedule.*'
```
Expected: PASS (all OpForkSchedule tests).

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/OpForkSchedule.h \
        bcos-evm-ref/opstack/OpForkSchedule.cpp \
        bcos-evm-ref/test/opstack/OpForkScheduleTest.cpp
git commit -m "$(cat <<'EOF'
feat(bcos-evm-ref): Add Jovian/Karst OpForkConfig placeholders

EOF
)"
```

---

### Task 2: OpFeeParams — unpack `da_footprint_gas_scalar`

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpFeeParams.h`
- Modify: `bcos-evm-ref/opstack/OpFeeParams.cpp`
- Modify: `bcos-evm-ref/test/opstack/OpFeeParamsTest.cpp`

**Interfaces:**
- Consumes: existing `unpackOpFeeParams(slot1,3,7,8)`
- Produces: `OpFeeParams.da_footprint_gas_scalar` (`uint16_t`, default `0`); unpack reads slot8 bytes `[18,20)` big-endian

- [ ] **Step 1: Write the failing test**

Extend `OpFeeParams.UnpacksScalarsFromPackedSlots` (or add new test) so slot8 also packs da scalar at `[18,20)`:

```cpp
TEST(OpFeeParams, UnpacksDaFootprintGasScalarFromSlot8)
{
    const auto slot1 = fullWord(1000);
    const auto slot3 = [] {
        evmc::bytes32 w = wordWith(16, 7, 4);
        auto blob = wordWith(20, 9, 4);
        for (size_t i = 0; i < 32; ++i)
            w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | blob.bytes[i]);
        return w;
    }();
    const auto slot7 = fullWord(2000);
    const auto slot8 = [] {
        // da=0x1234 at [18,20), opScalar=11 at [20,24), opConstant=13 at [24,32)
        evmc::bytes32 w = wordWith(18, 0x1234, 2);
        auto s = wordWith(20, 11, 4);
        auto c = wordWith(24, 13, 8);
        for (size_t i = 0; i < 32; ++i)
            w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | s.bytes[i] | c.bytes[i]);
        return w;
    }();

    const auto p = unpackOpFeeParams(slot1, slot3, slot7, slot8);
    EXPECT_EQ(p.da_footprint_gas_scalar, 0x1234u);
    EXPECT_EQ(p.operator_fee_scalar, 11u);
    EXPECT_EQ(p.operator_fee_constant, 13u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests -j8 2>&1 | tail -30
```
Expected: FAIL — no member `da_footprint_gas_scalar`.

- [ ] **Step 3: Write minimal implementation**

In `OpFeeParams.h`, add field (with default so existing designated inits keep compiling):

```cpp
    uint32_t operator_fee_scalar;
    uint64_t operator_fee_constant;
    uint16_t da_footprint_gas_scalar = 0;  // slot 8 bytes [18,20)
```

Update comment: remove「Jovian … 不解」; note Isthmus callers may ignore.

In `OpFeeParams.cpp` `unpackOpFeeParams` return:

```cpp
    return OpFeeParams{
        .l1_base_fee = intx::be::load<intx::uint256>(slot1),
        .base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 16, 4)),
        .blob_base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 20, 4)),
        .blob_base_fee = intx::be::load<intx::uint256>(slot7),
        .operator_fee_scalar = static_cast<uint32_t>(readBE(slot8, 20, 4)),
        .operator_fee_constant = readBE(slot8, 24, 8),
        .da_footprint_gas_scalar = static_cast<uint16_t>(readBE(slot8, 18, 2)),
    };
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests -j8 && \
  bcos-evm-ref/build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpFeeParams.*'
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/OpFeeParams.h \
        bcos-evm-ref/opstack/OpFeeParams.cpp \
        bcos-evm-ref/test/opstack/OpFeeParamsTest.cpp
git commit -m "$(cat <<'EOF'
feat(bcos-evm-ref): Unpack Jovian da_footprint_gas_scalar from L1Block slot8

EOF
)"
```

---

### Task 3: RollupCost — Jovian operator formula + `estimatedDaSize`

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/RollupCost.h`
- Modify: `bcos-evm-ref/opstack/RollupCost.cpp`
- Modify: `bcos-evm-ref/opstack/OpValidate.cpp` (pass `cfg` into `computeOperatorCost`)
- Modify: `bcos-evm-ref/opstack/OpTransition.cpp` (pass `cfg`; temporary until Task 7 return-type change)
- Modify: `bcos-evm-ref/test/opstack/RollupCostTest.cpp`

**Interfaces:**
- Consumes: `OpForkConfig` (Task 1), `OpFeeParams` (Task 2)
- Produces:
  - `intx::uint256 computeOperatorCost(const OpFeeParams&, uint64_t gas, const OpForkConfig& cfg) noexcept`
  - `uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept` — `estimatedDaSizeScaled(flz) / 1e6` (uint64); empty envelope → `0`
  - Remove or keep deprecated 2-arg `computeOperatorCost` **only if** all call sites updated in this task (prefer **replace** 2-arg with 3-arg)

- [ ] **Step 1: Write the failing test**

Append to `RollupCostTest.cpp`:

```cpp
#include <bcos-evm-ref/opstack/OpForkSchedule.h>

TEST(RollupCost, OperatorCostJovianUsesTimes100)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    // Isthmus: 1000*2000000/1e6 + 500 = 2500
    EXPECT_EQ(computeOperatorCost(p, 1000, isthmusConfig()), intx::uint256{2500});
    // Jovian: 1000*2000000*100 + 500 = 200000000500
    EXPECT_EQ(computeOperatorCost(p, 1000, jovianConfig()), intx::uint256{1000ull * 2000000ull * 100ull + 500});
    EXPECT_EQ(computeOperatorCost(p, 1000, karstConfig()),
        computeOperatorCost(p, 1000, jovianConfig()));
}

TEST(RollupCost, EstimatedDaSizeDividesScaledBy1e6)
{
    EXPECT_EQ(estimatedDaSize({}), 0u);
    // fastlz 0 → scaled floor 100e6 → size 100
    EXPECT_EQ(estimatedDaSizeScaled(0) / 1000000_u256, intx::uint256{100});
    std::vector<uint8_t> empty;
    EXPECT_EQ(estimatedDaSize(view(empty)), 0u);
    const auto env = readFixture("empty_tx.bin");
    const auto scaled = estimatedDaSizeScaled(flzCompressLen(view(env)));
    EXPECT_EQ(estimatedDaSize(view(env)), static_cast<uint64_t>(scaled / 1000000_u256));
}
```

Update existing `OperatorCostIsthmus` to pass `isthmusConfig()`.

- [ ] **Step 2: Run test to verify it fails**

Run build; Expected: FAIL — wrong arity / `estimatedDaSize` missing.

- [ ] **Step 3: Write minimal implementation**

`RollupCost.h`:

```cpp
#pragma once

#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

namespace bcos::evmref::opstack
{
uint32_t flzCompressLen(evmc::bytes_view data) noexcept;
intx::uint256 estimatedDaSizeScaled(uint32_t fastlzSize) noexcept;
uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept;
intx::uint256 computeL1Cost(const OpFeeParams& params, evmc::bytes_view signedTxEnvelope) noexcept;
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept;
}  // namespace bcos::evmref::opstack
```

`RollupCost.cpp` — replace `computeOperatorCost` and add:

```cpp
uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept
{
    if (signedTxEnvelope.empty())
        return 0;
    const auto scaled = estimatedDaSizeScaled(flzCompressLen(signedTxEnvelope));
    return static_cast<uint64_t>(scaled / intx::uint256{1'000'000});
}

intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept
{
    if (cfg.has_jovian_operator_formula)
    {
        return intx::uint256{gas} * intx::uint256{params.operator_fee_scalar} *
                   intx::uint256{100} +
               intx::uint256{params.operator_fee_constant};
    }
    return intx::uint256{gas} * intx::uint256{params.operator_fee_scalar} /
               intx::uint256{1'000'000} +
           intx::uint256{params.operator_fee_constant};
}
```

Update `OpValidate.cpp`:
```cpp
const auto opCost = cfg.has_operator_fee ?
                        computeOperatorCost(fee, static_cast<uint64_t>(tx.gas_limit), cfg) :
                        intx::uint256{0};
```

Update `OpTransition.cpp` operator call:
```cpp
const auto opAtUsed = computeOperatorCost(fee, static_cast<uint64_t>(gas_used), cfg);
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests -j8 && \
  ctest --test-dir bcos-evm-ref/build -R BcosEvmRefOpstackTests --output-on-failure
```
Expected: all opstack tests PASS.

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/RollupCost.h \
        bcos-evm-ref/opstack/RollupCost.cpp \
        bcos-evm-ref/opstack/OpValidate.cpp \
        bcos-evm-ref/opstack/OpTransition.cpp \
        bcos-evm-ref/test/opstack/RollupCostTest.cpp
git commit -m "$(cat <<'EOF'
feat(bcos-evm-ref): Fork-aware operator cost and estimatedDaSize

EOF
)"
```

---

### Task 4: Jovian precompile override table

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpPrecompiles.h`
- Modify: `bcos-evm-ref/opstack/OpPrecompiles.cpp`
- Modify: `bcos-evm-ref/opstack/OpForkSchedule.cpp` (point jovian/karst `precompiles` to jovian table)
- Modify: `bcos-evm-ref/test/opstack/OpPrecompilesTest.cpp`

**Interfaces:**
- Produces: `const PrecompileOverrides& jovianPrecompileOverrides() noexcept`
- Limits: `0x08→81984`, `0x0c→288960`, `0x0e→278784`, `0x0f→156672`, `0x100→gas 3450`

- [ ] **Step 1: Write the failing test**

```cpp
TEST(OpPrecompiles, JovianLimitsStricterThanIsthmus)
{
    const auto* j08 = jovianPrecompileOverrides().find(evmc::address{0x08});
    const auto* i08 = isthmusPrecompileOverrides().find(evmc::address{0x08});
    ASSERT_NE(j08, nullptr);
    ASSERT_NE(i08, nullptr);
    EXPECT_EQ(j08->max_input_size, 81984u);
    EXPECT_LT(j08->max_input_size, i08->max_input_size);

    EXPECT_EQ(jovianPrecompileOverrides().find(evmc::address{0x0c})->max_input_size, 288960u);
    EXPECT_EQ(jovianPrecompileOverrides().find(evmc::address{0x0e})->max_input_size, 278784u);
    EXPECT_EQ(jovianPrecompileOverrides().find(evmc::address{0x0f})->max_input_size, 156672u);
    EXPECT_EQ(jovianConfig().precompiles, &jovianPrecompileOverrides());
    EXPECT_EQ(karstConfig().precompiles, &jovianPrecompileOverrides());
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: `jovianPrecompileOverrides` undeclared / wrong limits.

- [ ] **Step 3: Write minimal implementation**

Declare in `OpPrecompiles.h` next to `isthmusPrecompileOverrides`.

`OpPrecompiles.cpp` add:

```cpp
constexpr PrecompileOverrides::Entry kJovianEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 81984},
    {.addr = evmc::address{0x100}, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 288960},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 278784},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 156672},
};
```

Wire `jovianConfig`/`karstConfig` `.precompiles = &jovianPrecompileOverrides()`.

- [ ] **Step 4: Run tests**

```bash
cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests -j8 && \
  bcos-evm-ref/build/test/bcos-evm-ref-opstack-tests --gtest_filter='OpPrecompiles.*:OpForkSchedule.*'
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/OpPrecompiles.h \
        bcos-evm-ref/opstack/OpPrecompiles.cpp \
        bcos-evm-ref/opstack/OpForkSchedule.cpp \
        bcos-evm-ref/test/opstack/OpPrecompilesTest.cpp
git commit -m "$(cat <<'EOF'
feat(bcos-evm-ref): Add Jovian precompile input-size overrides

EOF
)"
```

---

### Task 5: OpValidate P0-2 — reject empty envelope

**Files:**
- Modify: `bcos-evm-ref/opstack/OpValidate.cpp`
- Modify: `bcos-evm-ref/test/opstack/OpValidateTest.cpp`

**Interfaces:**
- Consumes: `opValidate(...)` existing signature
- Produces: if `signedTxEnvelope.empty()` → `std::errc::invalid_argument` (before L1 cost); blob check order unchanged (blob first)

- [ ] **Step 1: Write the failing test**

```cpp
TEST(OpValidate, EmptyEnvelopeFails)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 1000000000000000000000_u256};
    const auto r = opValidate(ts, blk(), baseTx(), {}, isthmusConfig(), OpFeeParams{}, 30000000);
    ASSERT_TRUE(std::holds_alternative<std::error_code>(r));
    EXPECT_EQ(std::get<std::error_code>(r), std::errc::invalid_argument);
}
```

Ensure `SufficientBalancePasses` / other tests that pass `{}` envelope are updated to pass a non-empty dummy envelope (e.g. one byte `{0x02}` is enough for P0-2; L1 cost may be non-zero — fund balance accordingly **or** use fixture `empty_tx.bin` bytes and keep fee zeros only if FastLZ path allows — simplest: `std::vector<uint8_t> env{0x02};` and large balance already in those tests).

- [ ] **Step 2: Run test to verify it fails**

Expected: `EmptyEnvelopeFails` FAIL (currently succeeds with `l1_cost=0`).

- [ ] **Step 3: Write minimal implementation**

In `OpValidate.cpp` after blob check:

```cpp
    if (signedTxEnvelope.empty())
        return make_error_code(std::errc::invalid_argument);
```

Fix any tests that relied on empty envelope success.

- [ ] **Step 4: Run full opstack tests**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/opstack/OpValidate.cpp bcos-evm-ref/test/opstack/OpValidateTest.cpp
git commit -m "$(cat <<'EOF'
fix(bcos-evm-ref): Reject empty signedTxEnvelope in opValidate (P0-2)

EOF
)"
```

---

### Task 6: OpReceiptMeta + `deriveOpReceiptMeta`

**Files:**
- Create: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpReceiptMeta.h`
- Create: `bcos-evm-ref/opstack/OpReceiptMeta.cpp`
- Create: `bcos-evm-ref/test/opstack/OpReceiptMetaTest.cpp`
- Modify: `bcos-evm-ref/CMakeLists.txt` (add `opstack/OpReceiptMeta.cpp`)
- Modify: `bcos-evm-ref/test/CMakeLists.txt` (add `OpReceiptMetaTest.cpp`)

**Interfaces:**
- Produces: types + `deriveOpReceiptMeta` per spec §2.4
- `fill_operator_scalars`: when true and (`operator_fee_scalar!=0` || `operator_fee_constant!=0`), set optional scalar/constant fields
- Isthmus (`!has_da_footprint`): leave da optionals empty
- Jovian: set `da_footprint_gas_scalar` and `da_footprint = estimatedDaSize(envelope) * scalar`
- Always set `l1_fee = l1_cost`; if `cfg.has_operator_fee`, set `operator_fee = operator_fee_at_used`

- [ ] **Step 1: Write the failing test**

```cpp
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpReceiptMeta.h>
#include <bcos-evm-ref/opstack/RollupCost.h>
#include <gtest/gtest.h>
#include <vector>

using namespace bcos::evmref::opstack;
using intx::operator""_u256;

TEST(OpReceiptMeta, IsthmusHasFeesWithoutDa)
{
    OpFeeParams fee{};
    fee.operator_fee_scalar = 11;
    fee.operator_fee_constant = 13;
    std::vector<uint8_t> env{0x02};
    const auto m = deriveOpReceiptMeta(isthmusConfig(), fee, {env.data(), env.size()},
        /*l1=*/100_u256, /*opUsed=*/50_u256, /*fill_operator_scalars=*/true);
    ASSERT_TRUE(m.l1_fee.has_value());
    EXPECT_EQ(*m.l1_fee, 100_u256);
    ASSERT_TRUE(m.operator_fee.has_value());
    EXPECT_EQ(*m.operator_fee, 50_u256);
    EXPECT_TRUE(m.operator_fee_scalar.has_value());
    EXPECT_FALSE(m.da_footprint.has_value());
}

TEST(OpReceiptMeta, JovianFillsDaFootprint)
{
    OpFeeParams fee{};
    fee.da_footprint_gas_scalar = 2;
    std::vector<uint8_t> env(50, 0x11);
    const auto size = estimatedDaSize({env.data(), env.size()});
    const auto m = deriveOpReceiptMeta(jovianConfig(), fee, {env.data(), env.size()},
        0_u256, 0_u256, false);
    ASSERT_TRUE(m.da_footprint_gas_scalar.has_value());
    EXPECT_EQ(*m.da_footprint_gas_scalar, 2u);
    ASSERT_TRUE(m.da_footprint.has_value());
    EXPECT_EQ(*m.da_footprint, size * 2u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: header / symbol missing.

- [ ] **Step 3: Write minimal implementation**

`OpReceiptMeta.h` — exact structs from spec §2.4 + `deriveOpReceiptMeta` declaration + `struct OpTxReceipt`.

`OpReceiptMeta.cpp`:

```cpp
#include <bcos-evm-ref/opstack/OpReceiptMeta.h>
#include <bcos-evm-ref/opstack/RollupCost.h>

namespace bcos::evmref::opstack
{
OpReceiptMeta deriveOpReceiptMeta(const OpForkConfig& cfg, const OpFeeParams& fee,
    evmc::bytes_view signedTxEnvelope, intx::uint256 l1_cost,
    intx::uint256 operator_fee_at_used, bool fill_operator_scalars) noexcept
{
    OpReceiptMeta m;
    m.l1_fee = l1_cost;
    if (cfg.has_operator_fee)
    {
        m.operator_fee = operator_fee_at_used;
        if (fill_operator_scalars &&
            (fee.operator_fee_scalar != 0 || fee.operator_fee_constant != 0))
        {
            m.operator_fee_scalar = fee.operator_fee_scalar;
            m.operator_fee_constant = fee.operator_fee_constant;
        }
    }
    if (cfg.has_da_footprint)
    {
        const auto scalar = static_cast<uint64_t>(fee.da_footprint_gas_scalar);
        m.da_footprint_gas_scalar = scalar;
        m.da_footprint = estimatedDaSize(signedTxEnvelope) * scalar;
    }
    return m;
}
}  // namespace bcos::evmref::opstack
```

Add sources to both CMakeLists.

- [ ] **Step 4: Run `OpReceiptMeta.*` tests**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/OpReceiptMeta.h \
        bcos-evm-ref/opstack/OpReceiptMeta.cpp \
        bcos-evm-ref/test/opstack/OpReceiptMetaTest.cpp \
        bcos-evm-ref/CMakeLists.txt bcos-evm-ref/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(bcos-evm-ref): Add OpReceiptMeta and deriveOpReceiptMeta

EOF
)"
```

---

### Task 7: OpTransition — P0-1 + return `OpTxReceipt`

**Files:**
- Modify: `bcos-evm-ref/include/bcos-evm-ref/opstack/OpTransition.h`
- Modify: `bcos-evm-ref/opstack/OpTransition.cpp`
- Modify: `bcos-evm-ref/test/opstack/OpTransitionTest.cpp`
- Modify: `bcos-evm-ref/test/opstack/OpBlockHarnessTest.cpp`
- Modify: `bcos-evm-ref/test/opstack/OpZeroDiffTest.cpp`

**Interfaces:**
- Consumes: `deriveOpReceiptMeta`, `computeOperatorCost(..., cfg)`
- Produces: `OpTxReceipt opTransition(..., const OpFeeParams& fee, uint64_t chainId, evmc::bytes_view signedTxEnvelope)`  
  **Signature note:** meta needs envelope — **add** `evmc::bytes_view signedTxEnvelope` parameter (same bytes as validate). Update all call sites.
- P0-1: replace unconditional L1 vault touch with:

```cpp
    if (props.l1_cost != 0)
        state.touch(OP_L1_FEE_VAULT).balance += props.l1_cost;
```

- After building `receipt`, set:

```cpp
    const auto opAtUsed = cfg.has_operator_fee ?
        computeOperatorCost(fee, static_cast<uint64_t>(gas_used), cfg) : intx::uint256{0};
    // ... vault settlement uses opAtUsed as today ...
    auto meta = deriveOpReceiptMeta(cfg, fee, signedTxEnvelope, props.l1_cost, opAtUsed,
        /*fill_operator_scalars=*/true);
    return OpTxReceipt{std::move(receipt), std::move(meta)};
```

- [ ] **Step 1: Write / adjust failing tests**

1. `OpTransition.RoutesFeesToFourVaults` — use non-empty env (already has); call `opTransition(..., fee, 1234, {env.data(), env.size()})`; use `txR.receipt` for status/diff.
2. `OpZeroDiff` — assert **no** `OP_L1_FEE_VAULT` in `opReceipt.receipt.state_diff.modified_accounts` **and** not in `deleted_accounts` when fee=0; pass non-empty envelope to validate+transition; use `.receipt` fields for comparisons.
3. Add:

```cpp
TEST(OpTransition, ZeroL1CostDoesNotTouchL1Vault)
{
    // minimal transfer, OpFeeParams{}, non-empty env, isthmusConfig
    // after opTransition, scan state_diff for OP_L1_FEE_VAULT — EXPECT none
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: signature mismatch and/or L1 vault still deleted/touched in zero-diff.

- [ ] **Step 3: Implement header + cpp + fix all call sites**

`OpTransition.h`:

```cpp
#include <bcos-evm-ref/opstack/OpReceiptMeta.h>
// ...
OpTxReceipt opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, const OpFeeParams& fee, uint64_t chainId,
    evmc::bytes_view signedTxEnvelope);
```

Implement P0-1 + meta return as above.

- [ ] **Step 4: Run full opstack suite**

```bash
cmake --build bcos-evm-ref/build --target bcos-evm-ref-opstack-tests -j8 && \
  ctest --test-dir bcos-evm-ref/build -R BcosEvmRefOpstackTests --output-on-failure
```
Expected: 100% PASS.

- [ ] **Step 5: Commit**

```bash
git add bcos-evm-ref/include/bcos-evm-ref/opstack/OpTransition.h \
        bcos-evm-ref/opstack/OpTransition.cpp \
        bcos-evm-ref/test/opstack/OpTransitionTest.cpp \
        bcos-evm-ref/test/opstack/OpBlockHarnessTest.cpp \
        bcos-evm-ref/test/opstack/OpZeroDiffTest.cpp
git commit -m "$(cat <<'EOF'
feat(bcos-evm-ref): Return OpTxReceipt and skip zero L1 vault touch

EOF
)"
```

---

### Task 8: Docs — README + vector-schema

**Files:**
- Modify: `bcos-evm-ref/README.md`
- Modify: `bcos-evm-ref/docs/vector-schema.md`

**Interfaces:** none (docs only)

- [ ] **Step 1: Update README milestone table**

Add/adjust rows:
- Jovian tx+receipt + Karst placeholder + Isthmus P0: done (or in-progress → done after this task)
- Explicit non-goals: block DA / extraData / E-b

- [ ] **Step 2: Update vector-schema**

- Document `_op_da_footprint_gas_scalar` → `OpFeeParams.da_footprint_gas_scalar`
- Fork: `["jovian"]` → `jovianConfig()`; `["karst"]` → `karstConfig()` (behavior≡Jovian)
- Expected receipts may include `_op_da_footprint` / `_op_l1_fee` as future harness fields (optional note)

- [ ] **Step 3: No code test** — skim for accuracy vs spec

- [ ] **Step 4: Commit**

```bash
git add bcos-evm-ref/README.md bcos-evm-ref/docs/vector-schema.md
git commit -m "$(cat <<'EOF'
docs(bcos-evm-ref): Document Jovian/Karst tx+receipt scope

EOF
)"
```

---

## Spec coverage self-check

| Spec requirement | Task |
|------------------|------|
| Jovian operator ×100 | Task 3 |
| Jovian precompile limits | Task 4 |
| `da_footprint_gas_scalar` unpack | Task 2 |
| `jovianConfig` / flags | Task 1 |
| `OpReceiptMeta` + derive | Task 6 |
| `opTransition` → `OpTxReceipt` | Task 7 |
| Karst placeholder | Task 1 (+4 pointer) |
| P0-1 L1 vault | Task 7 |
| P0-2 empty envelope | Task 5 |
| README / vector-schema | Task 8 |
| Non-goals (block DA, E-b) | Task 8 docs only |

**Placeholder scan:** none intentional.  
**Type consistency:** `OpTxReceipt` / `deriveOpReceiptMeta` / `computeOperatorCost(..., cfg)` / `estimatedDaSize` names match across tasks; Task 7 adds `signedTxEnvelope` to `opTransition` (required for meta — documented in Task 7 Interfaces).

---

## Execution Handoff

Plan complete and saved to `bcos-evm/docs/superpowers/plans/2026-07-10-bcos-evm-ref-jovian-karst-tx-receipt.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — execute tasks in this session with executing-plans checkpoints  

Which approach?
