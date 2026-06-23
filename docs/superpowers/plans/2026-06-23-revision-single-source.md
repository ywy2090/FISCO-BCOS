# Revision Gating Single Source of Truth — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make "given an `evmc_revision`, which EIP flags are on" a single canonical function (`revisionConfigFromRevision`), with FISCO feature-gating applied as a mask on top, and unify all consumers to read the resulting `RevisionConfig` bools instead of re-deriving from `revision >=`.

**Architecture:** Derive-then-mask. One kernel function `revisionConfigFromRevision(evmc_revision)` in `eth/RevisionConfig.h` produces the canonical (maximal) config. `EthPolicy`, `ForkProfileRegistry`, and `makeIsthmusRevisionConfig` call it directly. `FiscoPolicy` calls it then applies `applyFiscoFeatureGates` (an X-macro-generated mask over the A-class fields). Consumers (`EthHost.cpp` EIP-6780, `PrecompileActive.h` EIP-2537/7212) read the config bools.

**Tech Stack:** C++20, evmc, Boost.Test (`boost/test/included`), CTest, CMake, bash CI gate.

## Global Constraints

- `eth/` must never include `bcos/` or `opstack/` headers (ADR-005). The FISCO `field -> flag` map and `applyFiscoFeatureGates` live in the FISCO layer, never in the kernel.
- A-class gated fields (require explicit FISCO flag): `warm_access` (`feature_evm_eip2929`), `eip2537`/`eip7623`/`eip7702` (`feature_evm_prague`), `eip7212`/`eip7823` (`feature_evm_osaka`). All other bool fields are B-class (revision-only).
- All snapshot deltas are profile-only / runtime-inert (ADR-004). No runtime behavior may change; equivalence is the acceptance bar.
- `prague_post_execution` has no production consumer and stays `false` via struct default (no explicit overlay).
- The single source covers "EIP gating given a revision" only. `evmcRevisionFromBlockNumber` (Eth) and `toFiscoRevision` (FISCO) — the blockNum/features -> revision translation — stay in their policies and are out of scope for `derive`.
- Keep `REVISION_CONFIG_BOOL_FIELDS` count assert in sync (currently 13 fields).
- Build via the repo's standard CMake/CTest flow. Run the single test target after each task.

---

### Task 1: Canonical `revisionConfigFromRevision` + A-class macro (pure addition)

**Files:**
- Modify: `bcos-evm/eth/RevisionConfig.h`
- Test: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp`

**Interfaces:**
- Produces:
  - `bcos::evm_standard::RevisionConfig revisionConfigFromRevision(evmc_revision revision)` — canonical maximal config for a revision.
  - Macro `REVISION_CONFIG_GATED_FIELDS(X)` enumerating the 6 A-class field names.
  - `constexpr std::size_t revisionConfigGatedFieldCount()`.
- Consumes: existing `RevisionConfig`, `REVISION_CONFIG_BOOL_FIELDS`.

- [ ] **Step 1: Write the failing test** — add a `derive` snapshot case to `RevisionConfigProfileTest.cpp` inside the suite (after `revision_config_bool_field_macro_count`).

```cpp
BOOST_AUTO_TEST_CASE(derive_canonical_full_fork_snapshots)
{
    struct Row
    {
        evmc_revision revision;
        ExpectedRevisionConfig expected;
    };
    std::vector<Row> const rows = {
        {EVMC_LONDON, {.revision = EVMC_LONDON, .warm_access = true, .eip1559 = true}},
        {EVMC_PARIS, {.revision = EVMC_PARIS, .warm_access = true, .eip1559 = true}},
        {EVMC_SHANGHAI,
            {.revision = EVMC_SHANGHAI, .warm_access = true, .eip1559 = true, .eip3651 = true}},
        {EVMC_CANCUN, {.revision = EVMC_CANCUN,
                          .warm_access = true,
                          .eip1153 = true,
                          .eip4844 = true,
                          .eip5656 = true,
                          .eip6780 = true,
                          .eip1559 = true,
                          .eip3651 = true}},
        {EVMC_PRAGUE, {.revision = EVMC_PRAGUE,
                          .warm_access = true,
                          .eip2537 = true,
                          .eip7623 = true,
                          .eip1153 = true,
                          .eip4844 = true,
                          .eip5656 = true,
                          .eip6780 = true,
                          .eip1559 = true,
                          .eip3651 = true,
                          .eip7702 = true,
                          .calldata_floor_per_token = 10}},
        {EVMC_OSAKA, {.revision = EVMC_OSAKA,
                         .warm_access = true,
                         .eip2537 = true,
                         .eip7212 = true,
                         .eip7623 = true,
                         .eip7823 = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true,
                         .eip7702 = true,
                         .calldata_floor_per_token = 10}},
    };
    for (auto const& row : rows)
    {
        BOOST_TEST_CONTEXT("revision=" << row.revision)
        {
            assertRevisionConfigMatches(revisionConfigFromRevision(row.revision), row.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(gated_field_count_is_six)
{
    BOOST_CHECK_EQUAL(revisionConfigGatedFieldCount(), 6U);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest 2>&1 | rtk err`
Expected: compile FAIL — `revisionConfigFromRevision`/`revisionConfigGatedFieldCount` not declared.

- [ ] **Step 3: Write minimal implementation** — in `bcos-evm/eth/RevisionConfig.h`, add after the `REVISION_CONFIG_BOOL_FIELDS` block (before `makeIsthmusRevisionConfig`):

```cpp
// A-class feature-gated fields (FISCO requires an explicit flag ON for each).
#define REVISION_CONFIG_GATED_FIELDS(X) \
    X(warm_access)                      \
    X(eip2537)                          \
    X(eip7212)                          \
    X(eip7623)                          \
    X(eip7823)                          \
    X(eip7702)

inline constexpr std::size_t revisionConfigGatedFieldCount() noexcept
{
    std::size_t n = 0;
#define REVISION_CONFIG_GATED_COUNT(name) ++n;
    REVISION_CONFIG_GATED_FIELDS(REVISION_CONFIG_GATED_COUNT)
#undef REVISION_CONFIG_GATED_COUNT
    return n;
}

static_assert(revisionConfigGatedFieldCount() == 6,
    "Keep REVISION_CONFIG_GATED_FIELDS in sync with the A-class field set");

// Single source of truth: EIP gating for a given revision. Canonical (maximal) config.
// Chains translate blockNum/features -> revision elsewhere (EthPolicy/FiscoPolicy), then
// optionally mask A-class fields. Never read `revision >= EVMC_xxx` for a gated EIP outside here.
inline RevisionConfig revisionConfigFromRevision(evmc_revision revision)
{
    RevisionConfig cfg;
    cfg.revision = revision;
    cfg.warm_access = revision >= EVMC_BERLIN;
    cfg.eip1559 = revision >= EVMC_LONDON;
    cfg.eip3651 = revision >= EVMC_SHANGHAI;
    cfg.eip1153 = revision >= EVMC_CANCUN;
    cfg.eip4844 = revision >= EVMC_CANCUN;
    cfg.eip5656 = revision >= EVMC_CANCUN;
    cfg.eip6780 = revision >= EVMC_CANCUN;
    cfg.eip2537 = revision >= EVMC_PRAGUE;
    cfg.eip7623 = revision >= EVMC_PRAGUE;
    cfg.eip7702 = revision >= EVMC_PRAGUE;
    cfg.eip7212 = revision >= EVMC_OSAKA;
    cfg.eip7823 = revision >= EVMC_OSAKA;
    cfg.calldata_floor_per_token = cfg.eip7623 ? 10 : 0;
    return cfg;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest && ctest --test-dir build -R RevisionConfigProfileTest --output-on-failure 2>&1 | rtk err`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/RevisionConfig.h bcos-evm/test/eth/RevisionConfigProfileTest.cpp
rtk git commit -m "feat(evm): add canonical revisionConfigFromRevision + A-class field macro"
```

---

### Task 2: FISCO `applyFiscoFeatureGates` mask + completeness assert (pure addition)

**Files:**
- Modify: `bcos-evm/bcos/FiscoPolicy.h` (add free function + macro above `class FiscoPolicy`; do not yet wire it into `computeRevisionConfig`)
- Test: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp`

**Interfaces:**
- Produces:
  - Macro `FISCO_GATED_FLAG_MAP(X)` mapping each A-class `field` to its `ledger::Features::Flag`.
  - `void bcos::chain_policy::applyFiscoFeatureGates(bcos::evm_standard::RevisionConfig& cfg, const ledger::Features& features)` — `cfg.field &&= features.get(Flag::flag)` for each A-class field.
- Consumes: Task 1 `revisionConfigFromRevision`, `revisionConfigGatedFieldCount`, `REVISION_CONFIG_GATED_FIELDS`.

- [ ] **Step 1: Write the failing test** — add to `RevisionConfigProfileTest.cpp` (this file already includes `bcos-evm/bcos/FiscoPolicy.h`):

```cpp
BOOST_AUTO_TEST_CASE(apply_fisco_feature_gates_masks_only_a_class)
{
    using Flag = ledger::Features::Flag;
    // All flags OFF: every A-class field masked to false, B-class untouched.
    {
        auto cfg = revisionConfigFromRevision(EVMC_OSAKA);
        ledger::Features features;
        bcos::chain_policy::applyFiscoFeatureGates(cfg, features);
        BOOST_CHECK(!cfg.warm_access);
        BOOST_CHECK(!cfg.eip2537);
        BOOST_CHECK(!cfg.eip7212);
        BOOST_CHECK(!cfg.eip7623);
        BOOST_CHECK(!cfg.eip7823);
        BOOST_CHECK(!cfg.eip7702);
        // B-class survive.
        BOOST_CHECK(cfg.eip1153);
        BOOST_CHECK(cfg.eip4844);
        BOOST_CHECK(cfg.eip6780);
        BOOST_CHECK(cfg.eip1559);
        BOOST_CHECK(cfg.eip3651);
    }
    // Prague flag ON: prague-group A-class survive, osaka-group still masked.
    {
        auto cfg = revisionConfigFromRevision(EVMC_OSAKA);
        ledger::Features features;
        features.set(Flag::feature_evm_eip2929);
        features.set(Flag::feature_evm_prague);
        bcos::chain_policy::applyFiscoFeatureGates(cfg, features);
        BOOST_CHECK(cfg.warm_access);
        BOOST_CHECK(cfg.eip2537);
        BOOST_CHECK(cfg.eip7623);
        BOOST_CHECK(cfg.eip7702);
        BOOST_CHECK(!cfg.eip7212);
        BOOST_CHECK(!cfg.eip7823);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest 2>&1 | rtk err`
Expected: compile FAIL — `applyFiscoFeatureGates` not declared.

- [ ] **Step 3: Write minimal implementation** — in `bcos-evm/bcos/FiscoPolicy.h`, inside `namespace bcos::chain_policy` and after the anonymous-namespace `toFiscoRevision` block, add:

```cpp
// FISCO field -> feature flag map. X-macro keeps mask code and the completeness
// assert in one place. Flag identity stays in the FISCO layer (never in eth/).
#define FISCO_GATED_FLAG_MAP(X)            \
    X(warm_access, feature_evm_eip2929)    \
    X(eip2537, feature_evm_prague)         \
    X(eip7623, feature_evm_prague)         \
    X(eip7702, feature_evm_prague)         \
    X(eip7212, feature_evm_osaka)          \
    X(eip7823, feature_evm_osaka)

inline constexpr std::size_t fiscoGatedFlagMapCount() noexcept
{
    std::size_t n = 0;
#define FISCO_GATE_COUNT(field, flag) ++n;
    FISCO_GATED_FLAG_MAP(FISCO_GATE_COUNT)
#undef FISCO_GATE_COUNT
    return n;
}

// Completeness: every A-class kernel field has exactly one FISCO flag mapping.
static_assert(fiscoGatedFlagMapCount() == bcos::evm_standard::revisionConfigGatedFieldCount(),
    "FISCO_GATED_FLAG_MAP must cover every REVISION_CONFIG_GATED_FIELDS entry");

inline void applyFiscoFeatureGates(
    bcos::evm_standard::RevisionConfig& cfg, const ledger::Features& features)
{
    using Flag = ledger::Features::Flag;
#define FISCO_APPLY_GATE(field, flag) cfg.field = cfg.field && features.get(Flag::flag);
    FISCO_GATED_FLAG_MAP(FISCO_APPLY_GATE)
#undef FISCO_APPLY_GATE
}
```

Note: `FiscoPolicy.h` already includes `bcos-framework/ledger/Features.h` and (transitively) `RevisionConfig.h` via `FiscoRevisionConfig.h`. If `RevisionConfig.h` is not directly visible, add `#include "bcos-evm/eth/RevisionConfig.h"` at the top.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest && ctest --test-dir build -R RevisionConfigProfileTest --output-on-failure 2>&1 | rtk err`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/bcos/FiscoPolicy.h bcos-evm/test/eth/RevisionConfigProfileTest.cpp
rtk git commit -m "feat(evm): add FISCO feature-gate mask with compile-time completeness assert"
```

---

### Task 3: Re-point Eth-side derivation (EthPolicy + ForkProfileRegistry) to `derive`

**Files:**
- Modify: `bcos-evm/eth/vm/EthPolicy.h:27-43`
- Modify: `bcos-evm/specs-tests/src/ForkProfileRegistry.cpp:10-27`
- Test: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp:66-115` (`eth_policy_full_fork_snapshots`)

**Interfaces:**
- Consumes: Task 1 `revisionConfigFromRevision`.
- Produces: `EthPolicy::computeRevisionConfig` and `makeReferenceRevisionConfig` now densify `eip1559`/`eip3651` (canonical completeness). No runtime change (both fields profile-only).

- [ ] **Step 1: Update the snapshot expectations first (will fail until impl changes)** — replace the `rows` in `eth_policy_full_fork_snapshots` so every row carries the densified B-class fields:

```cpp
    std::vector<Row> const rows = {
        {15'537'394, {.revision = EVMC_PARIS, .warm_access = true, .eip1559 = true}},
        {17'034'870,
            {.revision = EVMC_SHANGHAI, .warm_access = true, .eip1559 = true, .eip3651 = true}},
        {19'426'587, {.revision = EVMC_CANCUN,
                         .warm_access = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true}},
        {22'000'000, {.revision = EVMC_PRAGUE,
                         .warm_access = true,
                         .eip2537 = true,
                         .eip7623 = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true,
                         .eip7702 = true,
                         .calldata_floor_per_token = 10}},
        {25'000'000, {.revision = EVMC_OSAKA,
                         .warm_access = true,
                         .eip2537 = true,
                         .eip7212 = true,
                         .eip7623 = true,
                         .eip7823 = true,
                         .eip1153 = true,
                         .eip4844 = true,
                         .eip5656 = true,
                         .eip6780 = true,
                         .eip1559 = true,
                         .eip3651 = true,
                         .eip7702 = true,
                         .calldata_floor_per_token = 10}},
    };
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest && ctest --test-dir build -R RevisionConfigProfileTest --output-on-failure 2>&1 | rtk err`
Expected: FAIL on `eth_policy_full_fork_snapshots` — `eip1559`/`eip3651` mismatch (EthPolicy still leaves them false).

- [ ] **Step 3: Re-point EthPolicy to `derive`** — replace the body of `EthPolicy::computeRevisionConfig` (`bcos-evm/eth/vm/EthPolicy.h:27-43`):

```cpp
    RevisionConfig computeRevisionConfig(const protocol::BlockHeader& header) const
    {
        return revisionConfigFromRevision(evmcRevisionFromBlockNumber(header.number()));
    }
```

- [ ] **Step 4: Re-point ForkProfileRegistry to `derive`** — replace `makeReferenceRevisionConfig` (`bcos-evm/specs-tests/src/ForkProfileRegistry.cpp:10-27`):

```cpp
bcos::evm_standard::RevisionConfig makeReferenceRevisionConfig(evmc_revision revision)
{
    return bcos::evm_standard::revisionConfigFromRevision(revision);
}
```

- [ ] **Step 5: Run both affected targets to verify pass**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest && ctest --test-dir build -R "RevisionConfigProfileTest|ForkProfileRegistry" --output-on-failure 2>&1 | rtk err`
Expected: PASS (ForkProfileRegistry `activatedEipsFor` output is unchanged — `eip1559`/`eip3651` are not in its EIP list).

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/vm/EthPolicy.h bcos-evm/specs-tests/src/ForkProfileRegistry.cpp bcos-evm/test/eth/RevisionConfigProfileTest.cpp
rtk git commit -m "refactor(evm): derive EthPolicy and ForkProfileRegistry from canonical revision config"
```

---

### Task 4: Re-point FiscoPolicy to `derive` + `applyFiscoFeatureGates`

**Files:**
- Modify: `bcos-evm/bcos/FiscoPolicy.h:49-87`
- Test: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp:117-188` (`fisco_policy_feature_gate_snapshots`)

**Interfaces:**
- Consumes: Task 1 `revisionConfigFromRevision`, Task 2 `applyFiscoFeatureGates`.
- Produces: `FiscoPolicy::computeRevisionConfig` EIP-bit derivation now goes through the single source; `eip3651` densifies to `true` (CANCUN floor). Outer FISCO bits (`fix_*`/`use_*`/`enable_*`) unchanged.

- [ ] **Step 1: Update snapshot expectations first** — add `.eip3651 = true` to all four rows in `fisco_policy_feature_gate_snapshots`. The four expected blocks become (only the added line shown in context):

```cpp
        // row 1 (CANCUN, eip2929):
            {.revision = EVMC_CANCUN, .warm_access = true, .eip1153 = true, .eip4844 = true,
                .eip5656 = true, .eip6780 = true, .eip1559 = true, .eip3651 = true}},
        // row 2 (PRAGUE): add .eip3651 = true alongside existing fields
        // row 3 (OSAKA): add .eip3651 = true alongside existing fields
        // row 4 (no flags, CANCUN): add .eip3651 = true alongside existing fields
```

Apply concretely — row 1:
```cpp
            {.revision = EVMC_CANCUN,
                .warm_access = true,
                .eip1153 = true,
                .eip4844 = true,
                .eip5656 = true,
                .eip6780 = true,
                .eip1559 = true,
                .eip3651 = true}},
```
row 2 (insert `.eip3651 = true,` after `.eip1559 = true,`):
```cpp
            {.revision = EVMC_PRAGUE,
                .warm_access = true,
                .eip2537 = true,
                .eip7623 = true,
                .eip1153 = true,
                .eip4844 = true,
                .eip5656 = true,
                .eip6780 = true,
                .eip1559 = true,
                .eip3651 = true,
                .eip7702 = true,
                .calldata_floor_per_token = 10}},
```
row 3 (insert `.eip3651 = true,` after `.eip1559 = true,`):
```cpp
            {.revision = EVMC_OSAKA,
                .warm_access = true,
                .eip2537 = true,
                .eip7212 = true,
                .eip7623 = true,
                .eip7823 = true,
                .eip1153 = true,
                .eip4844 = true,
                .eip5656 = true,
                .eip6780 = true,
                .eip1559 = true,
                .eip3651 = true,
                .eip7702 = true,
                .calldata_floor_per_token = 10}},
```
row 4:
```cpp
        {[&](ledger::Features& /*unused*/) {}, {.revision = EVMC_CANCUN,
                                                   .warm_access = false,
                                                   .eip1153 = true,
                                                   .eip4844 = true,
                                                   .eip5656 = true,
                                                   .eip6780 = true,
                                                   .eip1559 = true,
                                                   .eip3651 = true}},
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest && ctest --test-dir build -R RevisionConfigProfileTest --output-on-failure 2>&1 | rtk err`
Expected: FAIL on `fisco_policy_feature_gate_snapshots` — `eip3651` mismatch (FiscoPolicy still leaves it false).

- [ ] **Step 3: Re-point FiscoPolicy** — in `FiscoPolicy::computeRevisionConfig` (`bcos-evm/bcos/FiscoPolicy.h:49-87`), replace the per-field EIP block (the lines setting `ethCfg.warm_access` through `ethCfg.calldata_floor_per_token`, i.e. lines 57-69) with:

```cpp
        ethCfg = revisionConfigFromRevision(
            std::max(toFiscoRevision(m_features, header.version()), EVMC_CANCUN));
        applyFiscoFeatureGates(ethCfg, m_features);
```

Delete the now-redundant `ethCfg.revision = std::max(...)` assignment at line 55 (the `derive` call sets `revision`). Keep all `cfg.fix_* / cfg.use_* / cfg.enable_*` outer-bit assignments (lines 71-84) exactly as-is.

- [ ] **Step 4: Run to verify it passes**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest && ctest --test-dir build -R RevisionConfigProfileTest --output-on-failure 2>&1 | rtk err`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/bcos/FiscoPolicy.h bcos-evm/test/eth/RevisionConfigProfileTest.cpp
rtk git commit -m "refactor(evm): derive FiscoPolicy EIP gating from canonical config + feature mask"
```

---

### Task 5: Re-point `makeIsthmusRevisionConfig` to `derive(PRAGUE)`

**Files:**
- Modify: `bcos-evm/eth/RevisionConfig.h:62-73`
- Test: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp:45-56,190-193`

**Interfaces:**
- Consumes: Task 1 `revisionConfigFromRevision`.
- Produces: `makeIsthmusRevisionConfig()` returns the canonical Prague config (densified). `prague_post_execution` stays `false` by struct default (overlay removed). `isIsthmusOrchestrationProfile` unchanged (still keys on `eip7623 && eip7702 && eip4844`, all still true).

- [ ] **Step 1: Update the Isthmus assertion first** — replace `assertIsthmusHelperProfile` (`RevisionConfigProfileTest.cpp:45-56`) to expect the dense Prague profile, and rename the test case for clarity:

```cpp
inline void assertIsthmusHelperProfile(bcos::evm_standard::RevisionConfig const& actual)
{
    ExpectedRevisionConfig expected{};
    expected.revision = EVMC_PRAGUE;
    expected.warm_access = true;
    expected.eip2537 = true;
    expected.eip7623 = true;
    expected.eip7702 = true;
    expected.eip1153 = true;
    expected.eip4844 = true;
    expected.eip5656 = true;
    expected.eip6780 = true;
    expected.eip1559 = true;
    expected.eip3651 = true;
    expected.prague_post_execution = false;
    expected.calldata_floor_per_token = 10;
    assertRevisionConfigMatches(actual, expected);
}
```

And rename the test case (`RevisionConfigProfileTest.cpp:190`):
```cpp
BOOST_AUTO_TEST_CASE(isthmus_helper_dense_profile_all_fields)
{
    assertIsthmusHelperProfile(makeIsthmusRevisionConfig());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bcos-evm && cmake --build build --target test-RevisionConfigProfileTest && ctest --test-dir build -R RevisionConfigProfileTest --output-on-failure 2>&1 | rtk err`
Expected: FAIL — Isthmus profile still sparse (`eip1153`/`eip2537`/... false).

- [ ] **Step 3: Re-point makeIsthmus** — replace `makeIsthmusRevisionConfig` (`bcos-evm/eth/RevisionConfig.h:62-73`):

```cpp
inline RevisionConfig makeIsthmusRevisionConfig()
{
    // OP-Stack Isthmus runs on the canonical Prague gate set; prague_post_execution
    // has no production consumer and stays false via struct default (future-removal candidate).
    return revisionConfigFromRevision(EVMC_PRAGUE);
}
```

- [ ] **Step 4: Run the Isthmus consumers to verify runtime inertness**

Run: `cd bcos-evm && cmake --build build && ctest --test-dir build -R "RevisionConfigProfileTest|Isthmus|OpStack" --output-on-failure 2>&1 | rtk err`
Expected: PASS — densified fields are profile-only; OP-Stack Isthmus orchestration tests unchanged.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/RevisionConfig.h bcos-evm/test/eth/RevisionConfigProfileTest.cpp
rtk git commit -m "refactor(evm): derive Isthmus helper profile from canonical Prague config"
```

---

### Task 6: Unify consumers to read `cfg` bools (EIP-2537/7212 precompiles + EIP-6780)

**Files:**
- Modify: `bcos-evm/eth/precompiled/PrecompileActive.h:41-62`
- Modify: `bcos-evm/eth/state/EthHost.cpp:211,242`
- Test: `bcos-evm/test/eth/EipPrecompileRevisionGateTest.cpp:33-38`

**Interfaces:**
- Consumes: canonical bools (`cfg.eip2537`, `cfg.eip7212`, `cfg.eip6780`) guaranteed dense after Tasks 3–5.
- Produces: no production behavior change. In every reachable config `cfg.eip2537 == (revision>=PRAGUE)`, `cfg.eip7212 == (revision>=OSAKA && flag)`, `cfg.eip6780 == (revision>=CANCUN)`; the seam now reads the bool, so a synthetic `revision>=PRAGUE` with `eip2537=false` correctly reports inactive.

> Hard dependency: Tasks 3–5 must be merged first, otherwise the bools the consumers now trust are not yet dense.

- [ ] **Step 1: Add/adjust the failing precompile-seam test** — in `EipPrecompileRevisionGateTest.cpp`, set `eip2537 = true` in the existing accept case and add a new "prague-but-flag-off rejects" case:

```cpp
BOOST_AUTO_TEST_CASE(isActivePrecompile_prague_accepts_bls)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2537 = true};
    auto const addr = precompileAddress(0x0b);
    BOOST_CHECK(precompiled::isActivePrecompile(EVMC_PRAGUE, cfg, addr));
}

BOOST_AUTO_TEST_CASE(isActivePrecompile_reads_eip2537_bool_not_revision)
{
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2537 = false};
    auto const addr = precompileAddress(0x0b);
    BOOST_CHECK(!precompiled::isActivePrecompile(EVMC_PRAGUE, cfg, addr));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bcos-evm && cmake --build build --target test-EipPrecompileRevisionGateTest && ctest --test-dir build -R EipPrecompileRevisionGateTest --output-on-failure 2>&1 | rtk err`
Expected: FAIL on `isActivePrecompile_reads_eip2537_bool_not_revision` — current code returns `true` for 0x0b at PRAGUE regardless of `cfg.eip2537`.

- [ ] **Step 3: Unify `isActivePrecompile`** — replace the body (`bcos-evm/eth/precompiled/PrecompileActive.h:41-62`):

```cpp
inline bool isActivePrecompile(evmc_revision /*revision*/,
    bcos::evm_standard::RevisionConfig const& cfg, evmc_address const& addr) noexcept
{
    if (isP256Precompile(addr))
    {
        return cfg.eip7212;
    }
    if (!isLowPrecompile(addr))
    {
        return false;
    }
    auto const suffix = addr.bytes[19];
    if (suffix >= 0x01 && suffix <= 0x0a)
    {
        return true;
    }
    if (suffix >= 0x0b && suffix <= 0x11)
    {
        return cfg.eip2537;
    }
    return false;
}
```

- [ ] **Step 4: Unify the EIP-6780 sites in EthHost** — in `bcos-evm/eth/state/EthHost.cpp`, replace both `m_revisionConfig.revision >= EVMC_CANCUN` gates with the canonical bool:
  - Line 211: `if (m_revisionConfig.revision >= EVMC_CANCUN && !wasCreatedInTx(addr))` -> `if (m_revisionConfig.eip6780 && !wasCreatedInTx(addr))`
  - Line 242: `if (m_revisionConfig.revision >= EVMC_CANCUN)` -> `if (m_revisionConfig.eip6780)`

- [ ] **Step 5: Run the full seam + regression set to verify pass**

Run: `cd bcos-evm && cmake --build build && ctest --test-dir build -R "EipPrecompileRevisionGateTest|BcosPrecompileRevisionGateTest|Bcos6780Selfdestruct|Bcos2537MsmGas|Eip2537Kernel|Bcos7212ExecuteViaHost" --output-on-failure 2>&1 | rtk err`
Expected: PASS — precompile gate tests + EIP-6780 selfdestruct + EIP-2537/7212 regressions all green (equivalence preserved).

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/precompiled/PrecompileActive.h bcos-evm/eth/state/EthHost.cpp bcos-evm/test/eth/EipPrecompileRevisionGateTest.cpp
rtk git commit -m "refactor(evm): read EIP-2537/7212/6780 gates from RevisionConfig bools"
```

---

### Task 7: CI grep gate for A-class author-side rule

**Files:**
- Create: `bcos-evm/tools/ci/check-revision-single-source.sh`
- Modify: `.github/workflows/capability-gate.yml`

**Interfaces:**
- Consumes: A-class field names from `REVISION_CONFIG_GATED_FIELDS`. Scope (Q6): only the assignment pattern `<gatedField> = ... revision >= EVMC_` outside `RevisionConfig.h`. Bare `revision >=` consumer reads are intentionally NOT gated (indistinguishable from legitimate opcode-gas comparisons); consumer correctness rides on equivalence tests.

- [ ] **Step 1: Write the gate script**

```bash
#!/usr/bin/env bash
# Enforce single source of truth for A-class EIP revision gating.
# Fails if any A-class field is assigned directly from `revision >= EVMC_xxx`
# outside the canonical kernel header (RevisionConfig.h).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# A-class fields (keep in sync with REVISION_CONFIG_GATED_FIELDS in eth/RevisionConfig.h).
FIELDS=(warm_access eip2537 eip7212 eip7623 eip7823 eip7702)

status=0
for field in "${FIELDS[@]}"; do
    # Match e.g. `cfg.eip2537 = ... revision >= EVMC_PRAGUE` on one line.
    matches="$(grep -rnE "\.${field}[[:space:]]*=.*revision[[:space:]]*>=[[:space:]]*EVMC_" \
        --include='*.h' --include='*.hpp' --include='*.cpp' \
        --exclude='RevisionConfig.h' . || true)"
    if [[ -n "$matches" ]]; then
        echo "ERROR: A-class field '${field}' derived from 'revision >= EVMC_' outside RevisionConfig.h:" >&2
        echo "$matches" >&2
        echo "  -> Use revisionConfigFromRevision() (+ applyFiscoFeatureGates for FISCO) instead." >&2
        status=1
    fi
done

if [[ $status -eq 0 ]]; then
    echo "revision-single-source gate: OK (no A-class field derived outside RevisionConfig.h)"
fi
exit $status
```

- [ ] **Step 2: Make executable and run against the current (clean) tree**

Run: `chmod +x bcos-evm/tools/ci/check-revision-single-source.sh && bcos-evm/tools/ci/check-revision-single-source.sh`
Expected: prints `revision-single-source gate: OK` and exits 0 (Tasks 3–5 removed all out-of-header derivations).

- [ ] **Step 3: Verify the gate actually catches a violation**

Run:
```bash
printf 'cfg.eip2537 = revision >= EVMC_PRAGUE;\n' > /tmp/violation.cpp
cp /tmp/violation.cpp bcos-evm/__gate_probe.cpp
bcos-evm/tools/ci/check-revision-single-source.sh; echo "exit=$?"
rm bcos-evm/__gate_probe.cpp
```
Expected: prints the ERROR block and `exit=1`.

- [ ] **Step 4: Wire into the workflow** — add a step to `.github/workflows/capability-gate.yml` (after checkout, in the existing gate job):

```yaml
      - name: Revision single-source gate
        run: bcos-evm/tools/ci/check-revision-single-source.sh
```

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/tools/ci/check-revision-single-source.sh .github/workflows/capability-gate.yml
rtk git commit -m "ci(evm): add revision single-source A-class grep gate"
```

---

### Task 8: Documentation (capability-matrix, ADR-004 update, ADR-018 new)

**Files:**
- Modify: `bcos-evm/capability-matrix.md`
- Modify: `bcos-evm/docs/adr/004-revision-config-field-consumption.md`
- Create: `bcos-evm/docs/adr/018-revision-gating-single-source.md`

**Interfaces:** documentation only; matrix CI may enforce row format — keep existing token conventions.

- [ ] **Step 1: Update capability-matrix** — for the `RevisionConfig` EIP rows (`eip1153`/`eip6780`/`eip2537`/`eip7212` etc.), change the derivation note to "via `revisionConfigFromRevision` (single source)"; rewrite the FIX-12 Wave-2 note to state the Isthmus helper profile is now dense (canonical Prague) rather than sparse.

- [ ] **Step 2: Update ADR-004** — add a paragraph: profile-only fields (`eip1559`, `eip3651`, `prague_post_execution`) are now assigned by the single `revisionConfigFromRevision`; `eip1559`/`eip3651` are densified for canonical completeness and remain runtime-inert; `prague_post_execution` stays `false` by struct default and is a future-removal candidate.

- [ ] **Step 3: Create ADR-018** — record the decision:

```markdown
# ADR-018: Revision gating single source of truth

## Status
Accepted (2026-06-23)

## Context
EIP-to-revision derivation was duplicated across EthPolicy, FiscoPolicy,
ForkProfileRegistry, and makeIsthmusRevisionConfig, and re-derived at read
sites (PrecompileActive, EthHost). Drift had already produced a sparse
Isthmus profile.

## Decision
- `eth/RevisionConfig.h::revisionConfigFromRevision(evmc_revision)` is the
  single canonical "given a revision, which EIPs are on" function.
- FISCO feature-gating is a mask applied on top via
  `applyFiscoFeatureGates` + the `FISCO_GATED_FLAG_MAP` X-macro, whose
  completeness is asserted at compile time against
  `REVISION_CONFIG_GATED_FIELDS`.
- Consumers read `RevisionConfig` bools, not `revision >= EVMC_xxx`.
- A CI grep gate forbids deriving A-class fields from `revision >=` outside
  the kernel header; consumer-side correctness is guarded by equivalence tests.
- blockNum/features -> revision translation stays in the policies and is out
  of scope for the single source.

## Consequences
- EthPolicy/FiscoPolicy/Isthmus snapshots densify (`eip1559`/`eip3651`, and
  the full Prague set for Isthmus); all changes are profile-only / runtime-inert.
```

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/capability-matrix.md bcos-evm/docs/adr/004-revision-config-field-consumption.md bcos-evm/docs/adr/018-revision-gating-single-source.md
rtk git commit -m "docs(evm): record revision single-source decision (ADR-018) + matrix/ADR-004 updates"
```

---

## Self-Review

**1. Spec coverage:**
- Spec §3/§4 derive-then-mask + components -> Tasks 1, 2, 3, 4, 5. ✓
- Spec §4 `FISCO_GATED_FLAG_MAP` + completeness assert (Q2) -> Task 2. ✓
- Spec §5 `eip1559`/`eip3651` densification (Q5) -> Tasks 1, 3, 4. ✓
- Spec §6/§7 consumer-side unification (Q7) -> Task 6. ✓
- Spec §8 snapshot deltas -> Tasks 3, 4, 5 (each updates its own snapshot). ✓
- Spec §9 equivalence tests -> Tasks 1, 2, 3, 4, 5, 6. ✓
- Spec §10 CI grep gate, A-class only (Q6) -> Task 7. ✓
- Spec §11 docs (matrix/ADR-004/ADR-018) -> Task 8. ✓
- Spec Q3 `prague_post_execution` default-false, overlay removed -> Task 5. ✓
- Spec Q4 derive boundary (translation stays in policies) -> preserved in Tasks 3, 4. ✓
- Spec Q8 phased green-at-each-step order -> Task numbering 1->8 with the Task 6 hard-dependency note. ✓

**2. Placeholder scan:** No TBD/TODO/"handle edge cases"/"similar to Task N". All code steps carry full code. ✓

**3. Type consistency:** `revisionConfigFromRevision` / `revisionConfigGatedFieldCount` / `REVISION_CONFIG_GATED_FIELDS` / `FISCO_GATED_FLAG_MAP` / `fiscoGatedFlagMapCount` / `applyFiscoFeatureGates` used identically across Tasks 1–8. Field names (`eip2537`, `eip7212`, `eip6780`, `eip1559`, `eip3651`, `warm_access`, `calldata_floor_per_token`) match `RevisionConfig` in `eth/RevisionConfig.h`. ✓
