# EEST Statetest Harness H2–H7 — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bring `EthEestStateGranular` to evmone-statetest operational parity (H2–H7 per approved spec), enabling nightly full-tree runs with fork-aware execution, slow-test filtering, and failure bucketing.

**Architecture:** Refactor granular runner from triple-profile brute scan → per-case `postByFork` + manifest profile map; extend `ForkProfileRegistry` with Berlin/London/Paris; wire CTest defaults and `scan-eest-failures.py` bucket taxonomy.

**Tech Stack:** C++20, GTest, CMake/CTest, Python 3, EEST v5.4.0 fixtures, evmone VM.

**Spec:** `bcos-evm/docs/superpowers/specs/2026-07-06-eest-statetest-integration-design.md` (Approved 2026-07-07)

**Baseline (frozen):** Manifest `eth-eest-state-full.json` **4140/4140**; H1 `EthEestStateGranularFull` CTest wired (~2723 file-level cases).

**Revision:** 2026-07-07 — post review pass (GTest filter fix, H4 manifest alignment, assertLevels, H6 granularity, H7 CLI flags).

---

## Task ↔ Harness ID Map

| Plan Task | Harness ID | Depends on |
|-----------|------------|------------|
| Task 1 | H2 — slow-test filter | — |
| Task 2 | H3 — CLI multi-path / `-k` | — |
| Task 3 | H5 — Berlin/London/Paris profiles | — |
| Task 4 | H4 — fork inference + assertLevels | H3, H5 |
| Task 5 | H6 — unsupported → SKIP | H4 |
| Task 6 | H7 — failure buckets | H4 (partial), H6 (full) |
| Task 7 | H8 — `--trace` (optional) | Execution Trace Phase 0 |

---

## Known Divergences (pre-H4)

These explain why granular ≠ manifest today; H4 must close them:

| Divergence | Manifest runner | Granular runner (current) | Fix in |
|------------|-----------------|---------------------------|--------|
| Profile selection | `forkProfileId` from manifest entry | brute `{cancun, prague, osaka}` | H4 |
| Post fork key | `tryListSubtests(tc, postFork \|\| profile.upstreamForkName)` | `profile.upstreamForkName` only | H4 |
| Execution profile | `resolveExecutionProfile(profile, postFork)` | raw profile | H4 |
| assertLevels | `transitional, expectException, stateRoot` | + `logsHash` | H4 Step 2b |
| Prague dirs | `eth-osaka` for 7623/7702 (see table below) | `eth-prague` scan | H4 path map |

### Manifest path → execution profile (M8 reference)

Loaded from `eth-eest-state-full.json` at runtime (H4) or hard-coded fallback:

| casePath suffix | forkProfileId | postFork (default) |
|-----------------|---------------|---------------------|
| `prague/eip7623_increase_calldata_cost` | `eth-osaka` | Osaka |
| `prague/eip7702_set_code_tx` | `eth-osaka` | Osaka |
| `prague/eip2537_bls_12_381_precompiles` | `eth-prague` | Prague |
| `cancun/*`, `shanghai/*`, `osaka/*` | matches dir fork | same as profile |

---

## Global Constraints

1. **Do not regress manifest parity.** After every task: `EthExecutionSpecStateTests --manifest eth-eest-state-full.json` must stay **4140/0**.
2. **Smoke stays green:** `ctest -L specs-tests-smoke --test-dir build-bcos-evm-check` → 37/37.
3. **Minimal diff:** Harness-only changes in `test/eth-eest-test/` unless H5 requires `RevisionConfig` truth (already supports Berlin+).
4. **No new JSON deps** — reuse `boost::property_tree` / existing loader.
5. **RTK prefix** on shell commands in reports; plan commands are copy-paste ready.
6. **H8 (`--trace`) is optional** — implement only after Execution Trace Phase 0 lands; do not block H2–H7.

---

## Task 1: H2 — Slow-test default gtest filter

**Files:**
- Create: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/EestGranularSlowFilter.h`
- Modify: `bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp`

**Files NOT changed:** `CMakeLists.txt` for `EthEestStateGranularFull` (runner owns default filter).

### Step 1: Define slow-test filter string

Create `EestGranularSlowFilter.h`:

```cpp
#pragma once
#include <string_view>

namespace bcos::evm::reference_tests
{
/// evmone statetest slow patterns + EEST native slow cases.
/// Full nightly run: pass `--gtest_filter=*` on CLI to override.
inline constexpr std::string_view kEestGranularDefaultGtestFilter =
    "-"
    // evmone legacy GST (still present in some imported dirs)
    "stCreateTest.CreateOOGafterMaxCodesize:"
    "stQuadraticComplexityTest.Call50000_sha256:"
    "stTimeConsuming.static_Call50000_sha256:"
    "stTimeConsuming.CALLBlake2f_MaxRounds:"
    "VMTests/vmPerformance.*:"
    // EEST native slow / unbounded loops
    "*run_until_out_of_gas*:"
    "*test_run_until_out_of_gas*:"
    "*sha256*50000*:"
    "*CALLBlake2f*MaxRounds*";
}
```

Reference: `blockchain-impl/evmone/test/statetest/statetest.cpp` lines 112–121.

### Step 2: Apply default filter in runner `main`

In `EthEestStateGranular.cpp`, **before** `testing::InitGoogleTest` (match evmone-statetest):

```cpp
#include "bcos-evm/eth-eest-test/EestGranularSlowFilter.h"
// ...
testing::FLAGS_gtest_filter = std::string(kEestGranularDefaultGtestFilter);
testing::InitGoogleTest(&argc, argv);  // CLI --gtest_filter overrides default
```

**Do not** guard on `GTEST_FLAG(gtest_filter) != "*"` — GTest default is `"*"` pre-init, so an if/else would skip setting the slow filter entirely.

### Step 3: CTest wiring

**Single source of truth:** runner default (Step 2). Do **not** add a partial duplicate filter to `EthEestStateGranularFull` in CMake.

```cmake
# unchanged
add_test(NAME EthEestStateGranularFull
    COMMAND EthEestStateGranular ${EVM_REF_EEST_ROOT}/fixtures/state_tests)
```

`EthEestStateGranularSmoke` keeps its existing positive filter (`*eip1559*:*eip5656*:*eip3855*`) — InitGoogleTest overrides the runner default when CTest passes `--gtest_filter=...`.

### Step 4: Verify

```bash
cd build-bcos-evm-check
cmake --build . --target EthEestStateGranular -j$(sysctl -n hw.ncpu)

# Path must remain first positional arg until H3 lands
EEST=build-bcos-evm-check/_deps/evm_ref_eest_root/fixtures/state_tests
GRAN=./bcos-evm/test/eth-eest-test/EthEestStateGranular

$GRAN $EEST/cancun/eip1153_tstore --gtest_list_tests 2>/dev/null | wc -l
$GRAN $EEST/cancun/eip1153_tstore --gtest_filter='*run_until_out_of_gas*' --gtest_list_tests 2>/dev/null | wc -l

ctest -R EthEestStateGranularSmoke --test-dir build-bcos-evm-check
```

**Acceptance:** Default filter excludes slow case names vs `--gtest_filter=*`; smoke passes.

### Step 5: Commit

```bash
git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/EestGranularSlowFilter.h \
        bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp
git commit -m "$(cat <<'EOF'
test(eest): H2 default slow-test gtest filter for EthEestStateGranular

Align nightly granular runs with evmone-statetest slow exclusions so
full-tree CTest completes in bounded time without hiding fast regressions.
EOF
)"
```

---

## Task 2: H3 — CLI multi-path + `-k` substring filter

**Files:**
- Create: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/EestGranularCli.h`
- Create: `bcos-evm/test/eth-eest-test/src/EestGranularCli.cpp`
- Modify: `bcos-evm/test/eth-eest-test/CMakeLists.txt` — add `EestGranularCli.cpp` to `bcos-evm-specs-tests-core`
- Modify: `bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp`
- Test: `bcos-evm/test/eth-eest-test/test/EestGranularCliTest.cpp` (via `add_reference_test`)

### Step 1: Write failing CLI parse test

Test parses **post-InitGoogleTest** argv (GTest flags already stripped):

```cpp
#define BOOST_TEST_MODULE EestGranularCliTest
#include "bcos-evm/eth-eest-test/EestGranularCli.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(parse_multi_path_and_k_filter)
{
    const char* argv[] = {"prog", "path/a", "path/b", "-k", "4844", "--fork-profiles", "eth-cancun"};
    int argc = 7;
    auto parsed = parseEestGranularCliRemaining(argc, const_cast<char**>(argv));
    BOOST_REQUIRE_EQUAL(parsed.paths.size(), 2u);
    BOOST_REQUIRE(parsed.nameFilter.has_value());
    BOOST_CHECK_EQUAL(*parsed.nameFilter, "4844");
    BOOST_REQUIRE_EQUAL(parsed.profileIds.size(), 1u);
    BOOST_CHECK_EQUAL(parsed.profileIds[0], "eth-cancun");
}
```

### Step 2: Implement CLI parser

`EestGranularCli.h`:

```cpp
struct EestGranularCliOptions
{
    std::vector<std::filesystem::path> paths;
    std::optional<std::string> nameFilter;  // -k
    std::vector<std::string> profileIds;    // empty => runner defaults
};

/// Parse argv AFTER testing::InitGoogleTest (gtest flags removed).
EestGranularCliOptions parseEestGranularCliRemaining(int argc, char** argv);
```

**Parse order in `main`:**

1. Set slow-filter default → `testing::InitGoogleTest(&argc, argv)`.
2. `parseEestGranularCliRemaining(argc, argv)` for paths, `-k`, `--fork-profiles`.

Rules:
- ≥1 path required (file or directory).
- `-k <substring>` → skip GTest registration when test name doesn't contain substring.
- `--fork-profiles a,b,c` comma-separated.
- Unknown flags → error with usage (do not silently ignore).

Update usage string:

```
Usage: EthEestStateGranular <path> [<path>...] [-k SUBSTR] [--fork-profiles IDS]
       [--gtest_filter=...]   # standard GTest flags
```

### Step 3: Refactor `main` to loop paths

```cpp
testing::FLAGS_gtest_filter = std::string(kEestGranularDefaultGtestFilter);
testing::InitGoogleTest(&argc, argv);

auto opts = parseEestGranularCliRemaining(argc, argv);
if (opts.paths.empty()) { /* usage + exit 1 */ }

RunnerConfig config = buildRunnerConfig(opts.profileIds);
for (auto const& root : opts.paths)
{
    if (is_directory(root))
        registerFilesFromDirectory(root, &config, opts.nameFilter);
    else if (is_regular_file(root))
        registerSubtestsFromFile(root, &config, opts.nameFilter);
    else { /* error */ }
}
return RUN_ALL_TESTS();
```

Extract `buildRunnerConfig(profileIds)` — default manifest-16 list when empty (see H4).

### Step 4: Verify

```bash
GRAN=build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthEestStateGranular
EEST=build-bcos-evm-check/_deps/evm_ref_eest_root/fixtures/state_tests

$GRAN $EEST/cancun/eip4844_blobs $EEST/cancun/eip6780_selfdestruct \
  -k insufficient --fork-profiles eth-cancun --gtest_list_tests

$GRAN $EEST/prague/eip7702_set_code_tx/set_code_txs/ \
  -k valid --fork-profiles eth-osaka --gtest_filter=* --gtest_brief=1
```

### Step 5: Commit

```bash
git add bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/EestGranularCli.h \
        bcos-evm/test/eth-eest-test/src/EestGranularCli.cpp \
        bcos-evm/test/eth-eest-test/test/EestGranularCliTest.cpp \
        bcos-evm/test/eth-eest-test/CMakeLists.txt \
        bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp
git commit -m "$(cat <<'EOF'
test(eest): H3 multi-path and -k name filter for EthEestStateGranular

Match evmone-statetest CLI so engineers can slice directories and substring-
filter case names without manifest indirection.
EOF
)"
```

---

## Task 3: H5 — ForkProfile Berlin / London / Paris

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/src/ForkProfileRegistry.cpp`
- Modify: `bcos-evm/test/eth-eest-test/test/ForkProfileRegistryTest.cpp`

No `RevisionConfig.h` changes — `revisionConfigFromRevision` already gates from `EVMC_BERLIN` / `EVMC_LONDON` / `EVMC_PARIS`.

### Step 1: Add profiles

```cpp
ForkProfile makeLondonProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_LONDON);
    ForkProfile profile;
    profile.profileId = "eth-london";
    profile.canonicalName = "Ethereum London";
    profile.aliases = {"London"};
    profile.upstreamForkName = "London";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);  // EIP-2929, EIP-1559
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}

ForkProfile makeParisProfile()
{
    auto const revision = makeReferenceRevisionConfig(EVMC_PARIS);
    ForkProfile profile;
    profile.profileId = "eth-paris";
    profile.canonicalName = "Ethereum Paris (Merge)";
    profile.aliases = {"Paris", "Merge"};  // EEST may use either
    profile.upstreamForkName = "Paris";
    profile.revision = revision;
    profile.activatedEips = activatedEipsFor(revision);
    profile.pathProfiles = {referenceParityProfile()};
    return profile;
}
```

Register order in ctor: **Berlin, London, Paris**, Shanghai, Cancun, Prague, Osaka.

### Step 2: Extend unit tests

```cpp
BOOST_AUTO_TEST_CASE(find_berlin_london_paris_by_upstream_fork)
{
    auto& reg = ForkProfileRegistry::instance();
    BOOST_REQUIRE(reg.findByUpstreamFork("Berlin").has_value());
    BOOST_REQUIRE(reg.findByUpstreamFork("London").has_value());
    BOOST_REQUIRE(reg.findByUpstreamFork("Paris").has_value());
    BOOST_REQUIRE(reg.findByUpstreamFork("Merge").has_value());  // alias
    BOOST_CHECK(reg.findByUpstreamFork("Paris")->revision.revision == EVMC_PARIS);
}
```

### Step 3: Verify

```bash
cmake --build build-bcos-evm-check --target ForkProfileRegistryTest -j$(sysctl -n hw.ncpu)
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/ForkProfileRegistryTest
```

### Step 4: Commit

```bash
git commit -m "$(cat <<'EOF'
test(eest): H5 add Berlin London Paris ForkProfiles for historical EEST dirs

Enables granular runner to resolve pre-Shanghai state_tests without
falling through to empty subtest lists or wrong revision defaults.
EOF
)"
```

---

## Task 4: H4 — Per-case fork inference (replace triple-profile scan)

**Files:**
- Create: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/EestForkInference.h`
- Create: `bcos-evm/test/eth-eest-test/src/EestForkInference.cpp`
- Modify: `bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp`
- Test: `bcos-evm/test/eth-eest-test/test/EestForkInferenceTest.cpp`

Add `EestForkInference.cpp` to `bcos-evm-specs-tests-core`.

### Step 1: API

```cpp
struct ResolvedSubtestRun
{
    ForkProfile executionProfile;  // after resolveExecutionProfile
    std::string postForkKey;       // key in postByFork / tryListSubtests arg
};

/// Optional path segment hint: cancun → Cancun (never sole source of truth).
std::optional<std::string_view> inferUpstreamForkFromPath(
    std::filesystem::path const& file,
    std::filesystem::path const& stateTestsRoot);

/// Lookup forkProfileId for manifest-16 dirs (prague/eip7623 → eth-osaka).
std::optional<std::string_view> manifestProfileIdForPath(
    std::filesystem::path const& file,
    std::filesystem::path const& stateTestsRoot);

/// Expand one StateTestCase into executable (profile, postFork) pairs.
std::vector<ResolvedSubtestRun> resolveRunsForCase(
    StateTestCase const& test,
    std::filesystem::path const& sourceFile,
    std::filesystem::path const& stateTestsRoot,
    std::vector<ForkProfile> const& profileFilter);
```

### Step 2: `resolveRunsForCase` algorithm

For each `(postForkKey, _)` in `test.postByFork`:

1. If `profileFilter` non-empty and no filter profile can run this post fork → skip key.
2. Determine base profile:
   - If `manifestProfileIdForPath(sourceFile)` hits → `findByProfileId(manifest id)`.
   - Else if exactly one profile in filter matches post fork → use it.
   - Else if path hint matches postForkKey → use filter profile whose `upstreamForkName` matches.
   - Else use first filter profile (when filter size == 1) or skip.
3. `executionProfile = registry.resolveExecutionProfile(*baseProfile, postForkKey)`.
4. Push `{executionProfile, postForkKey}`.

**Default profile filter** (when `--fork-profiles` omitted):

```cpp
static std::array<char const*, 4> kManifest16Profiles = {
    "eth-shanghai", "eth-cancun", "eth-prague", "eth-osaka"};
```

Do **not** include Berlin/London/Paris in default — WP-HIST scope.

### Step 3: Refactor runner loops

Replace triple-profile loop with:

```cpp
static constexpr std::array<char const*, 3> kDefaultAssertLevels = {
    "transitional", "expectException", "stateRoot"};

for (auto const& run : resolveRunsForCase(tc, m_file, stateTestsRoot, m_config->profiles))
{
    auto const subtests = tryListSubtests(tc, run.postForkKey);
    if (subtests.empty()) continue;

    EthMessageAdapter adapter(run.executionProfile, m_config->hashImpl, m_config->vm);
    for (auto const& st : subtests)
    {
        // ...
        synthetic.assertLevels = {kDefaultAssertLevels.begin(), kDefaultAssertLevels.end()};
    }
}
```

Apply same pattern in `registerSubtestsFromFile` (subtest-level registration).

**Per-case empty runs:** `continue` (not `GTEST_SKIP` entire file) — other cases in same JSON may still run.

### Step 4: M8 integration verify

```bash
GRAN=build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthEestStateGranular
EEST=build-bcos-evm-check/_deps/evm_ref_eest_root
ROOT=$EEST/fixtures/state_tests

# Cancun 4844 — eth-cancun
$GRAN $ROOT/cancun/eip4844_blobs --fork-profiles eth-cancun --gtest_filter=* --gtest_brief=1

# Prague 7623 — must use eth-osaka (manifest convention)
$GRAN $ROOT/prague/eip7623_increase_calldata_cost --fork-profiles eth-osaka --gtest_filter=* --gtest_brief=1

# Manifest regression (unchanged)
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthExecutionSpecStateTests \
  --manifest bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json \
  --eest-root $EEST \
  --expectations bcos-evm/test/eth-eest-test/manifests/expectations.json
```

**Acceptance:** Granular manifest-16 dirs → 0 failures with correct `--fork-profiles`; manifest full stays 4140/0.

### Step 5: Unit tests

```cpp
BOOST_AUTO_TEST_CASE(manifest_map_prague7623_uses_osaka)
{
    fs::path root = "/fixtures/state_tests";
    auto id = manifestProfileIdForPath(root / "prague/eip7623_increase_calldata_cost/x.json", root);
    BOOST_REQUIRE(id.has_value());
    BOOST_CHECK_EQUAL(*id, "eth-osaka");
}

BOOST_AUTO_TEST_CASE(resolve_runs_uses_post_fork_key)
{
    // fixture StateTestCase with postByFork["Osaka"] and profileFilter {eth-osaka}
    // expect postForkKey == "Osaka", not "Prague"
}
```

### Step 6: Commit

```bash
git commit -m "$(cat <<'EOF'
test(eest): H4 per-case fork inference and manifest assertLevels in granular

Stop brute-force triple-profile scanning; align post fork keys, execution
profiles, and assertLevels with EthExecutionSpecStateTests manifest runner.
EOF
)"
```

---

## Task 5: H6 — Unsupported formats → GTEST_SKIP

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/src/GeneralStateTestLoader.cpp`
- Modify: `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/GeneralStateTestLoader.h`
- Modify: `bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp`
- Test: `bcos-evm/test/eth-eest-test/test/GeneralStateTestLoaderTest.cpp`

### Step 1: Add non-throwing loader

```cpp
enum class StateTestLoadStatus { Ok, UnsupportedFormat, ParseError };

struct StateTestLoadResult
{
    StateTestLoadStatus status;
    std::vector<StateTestCase> cases;
    std::string reason;
};

StateTestLoadResult tryLoadGeneralStateTestFile(std::filesystem::path const& jsonPath);
```

Map throw paths:
- Empty map / engine-only JSON / missing transaction env → `UnsupportedFormat`
- Malformed JSON / IO error → `ParseError`

Keep `loadGeneralStateTestFile` / `loadEestStateTestFile` throwing for manifest runners.

### Step 2: Granular skip behavior

| Scope | Behavior |
|-------|----------|
| File-level test, load failure | `GTEST_SKIP() << reason` in `TestBody` |
| File-level test, zero cases after load | `GTEST_SKIP() << "no cases"` |
| File-level test, all post forks unknown | `GTEST_SKIP() << "no supported forks"` |
| Single case, zero resolved runs | `continue` (sibling cases may run) |
| Registration time (subtest mode) | do not register tests for skipped files |

Do **not** `GTEST_SKIP()` inside inner loop over cases — skips entire file incorrectly.

### Step 3: Fixture + test

Add minimal stub under `test/eth-eest-test/assets/eest/unsupported/not_gst.json` (e.g. `{ "blocks": [] }`).

### Step 4: Verify

```bash
GRAN=build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthEestStateGranular
$GRAN bcos-evm/test/eth-eest-test/assets/eest/unsupported/not_gst.json 2>&1 | grep -i skip
```

### Step 5: Commit

```bash
git commit -m "$(cat <<'EOF'
test(eest): H6 skip unsupported state test JSON in granular runner

Treat engine-api and non-GST fixtures as SKIP instead of hard FAIL so
nightly full-tree runs classify noise separately from execution failures.
EOF
)"
```

---

## Task 6: H7 — Failure bucket reports (JSON + MD)

**Files:**
- Modify: `bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py`
- Create: `bcos-evm/test/eth-eest-test/tools/bucket-failures.py`
- Create: `bcos-evm/test/eth-eest-test/reports/README.md`

### Step 1: Bucket taxonomy (`bucket-failures.py`)

| Bucket | Pattern (ordered) |
|--------|-------------------|
| `state_root` | `stateRoot mismatch`, `state root` |
| `logs_hash` | `logsHash`, `logs hash` |
| `gas_used` | `gasUsed`, `gas used`, `intrinsic gas` |
| `expect_exception` | `expectException`, `expected reverted` |
| `balance_nonce` | `balance`, `nonce`, `coinbase` |
| `code_storage` | `code mismatch`, `storage` |
| `runner_error` | `no gtest xml`, `timeout`, `SEGV` |
| `unknown` | default |

Output: `reports/eest-granular-failures-<timestamp>.{json,md}`

### Step 2: Extend `scan-eest-failures.py`

Replace hard-coded `MANIFEST_DIRS` with manifest loader:

```python
def load_manifest_dirs(manifest_path: Path) -> list[tuple[str, str]]:
    data = json.loads(manifest_path.read_text())
    out = []
    for e in data["entries"]:
        rel = e["casePath"].removeprefix("fixtures/state_tests/")
        out.append((rel, e["forkProfileId"]))
    return out
```

CLI via `argparse`:

| Flag | Action |
|------|--------|
| `--manifest-16` (default) | Scan all 16 dirs from `eth-eest-state-full.json` |
| `--dir REL` | Single directory under `state_tests/` |
| `--profile ID` | Override fork profile (default from manifest) |
| `--granular-full` | One full-tree run + XML parse (nightly CI artifact) |
| `--build-dir PATH` | Default `build-bcos-evm-check` |

Post-process failures through `bucket-failures.py`.

### Step 3: CMake custom target (maintainers only, not PR gate)

```cmake
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_target(eest-granular-failure-report
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/scan-eest-failures.py --manifest-16
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generate EEST granular failure bucket report")
endif()
```

### Step 4: Verify

```bash
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py \
  --dir cancun/eip4844_blobs --profile eth-cancun

python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --manifest-16
```

### Step 5: Commit

```bash
git commit -m "$(cat <<'EOF'
test(eest): H7 failure bucket reports for granular statetest runs

Add JSON/MD taxonomy so nightly full-tree failures group by assertion
kind (stateRoot, logs, gas, exception) for parity loop prioritization.
EOF
)"
```

---

## Task 7 (Optional): H8 — `--trace` flag stub

**Depends on:** `bcos-evm/docs/superpowers/specs/2026-07-06-evm-execution-trace-design.md` Phase 0.

**Files:** `EestGranularCli.cpp`, `EthEestStateGranular.cpp`

When Execution Trace Phase 0 **not** merged: `--trace` → `setenv("EEST_PROBE","1",1)` only; document in `--help`.

When Phase 0 **merged:** wire `TraceGate` tier-A; upgrade without CLI break.

---

## Final Verification (all tasks)

```bash
REPO=/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
cd $REPO
cmake --build build-bcos-evm-check --target EthEestStateGranular EthExecutionSpecStateTests \
  ForkProfileRegistryTest EestGranularCliTest EestForkInferenceTest -j$(sysctl -n hw.ncpu)

# Manifest regression guard
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthExecutionSpecStateTests \
  --manifest bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json \
  --eest-root build-bcos-evm-check/_deps/evm_ref_eest_root \
  --expectations bcos-evm/test/eth-eest-test/manifests/expectations.json

# Smoke + nightly labels
ctest -L specs-tests-smoke --test-dir build-bcos-evm-check --output-on-failure
ctest -R EthEestStateGranularSmoke --test-dir build-bcos-evm-check
ctest -R EthEestStateGranularFull --test-dir build-bcos-evm-check -L nightly --timeout 14400

# Bucket report
python3 bcos-evm/test/eth-eest-test/tools/scan-eest-failures.py --manifest-16
```

**Done when:**

| Criterion | Target |
|-----------|--------|
| Spec §5 H2–H7 acceptance | All rows satisfied |
| Manifest full | **4140/4140** |
| Granular manifest-16 | 0 failures with manifest `forkProfileId` |
| Berlin/London/Paris | Runnable under `--fork-profiles eth-berlin` etc.; failures bucketed not crashed |
| Full tree nightly | Completes with slow filter; SKIP for unsupported JSON |

---

## Execution Order & Parallelism

```
        ┌─ H2 (filter) ─────────────┐
        ├─ H3 (CLI) ────────────────┤
H5 ─────┤                           ├──→ H4 ──→ H6 ──→ H7
(profiles)                           │
        └────────────────────────────┘
H8 optional after Execution Trace Phase 0
```

| Task | Est. | Parallel with |
|------|------|---------------|
| H2 | 1h | H3, H5 |
| H3 | 2h | H2, H5 |
| H5 | 1.5h | H2, H3 |
| H4 | 3h | after H3 + H5 |
| H6 | 2h | after H4 |
| H7 | 2h | after H4 (partial); full after H6 |

**Recommended:** One subagent per task; manifest 4140/0 check after every merge.

---

## Out of Scope

| Item | Track |
|------|-------|
| WP-HIST execution parity (Berlin/London/Paris fails) | Separate parity loop after H7 buckets |
| `--strict-logs` granular mode | Future CLI if logsHash audit needed |
| Execution Trace Phase 0 | Unblocks H8 proper |
| Blockchain tests | `eest-blockchain-test-runner-parity-design.md` |
| `EthGSTGranular` (legacy GST) | Separate; not in this plan |

---

## Review Changelog

| Date | Change |
|------|--------|
| 2026-07-07 | Initial plan from approved spec |
| 2026-07-07 | Review pass: fix GTest filter if/else bug; remove duplicate CMake filter; H4 manifest profile map + `resolveExecutionProfile`; assertLevels Step 2b; H6 skip granularity; H7 argparse; Task↔H map; known divergences table |
