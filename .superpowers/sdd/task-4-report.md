# Task 4 Report: H4 — Per-case Fork Inference + assertLevels Alignment

## Status

**COMPLETE**

## Commit

- `<SHA>` — test(eest): H4 per-case fork inference and manifest assertLevels in granular

## Changes

### Created `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/EestForkInference.h`

- `ResolvedSubtestRun` — execution profile + `postForkKey`
- `inferUpstreamForkFromPath` — path segment hint (cancun → Cancun)
- `manifestProfileIdForPath` — manifest-16 profile lookup with prague/eip7623 & eip7702 → eth-osaka overrides
- `resolveRunsForCase` — expand `postByFork` into executable (profile, postFork) pairs

### Created `bcos-evm/test/eth-eest-test/src/EestForkInference.cpp`

- Implements fork inference algorithm: filter by matching post fork, manifest path overrides, path hints, single-profile fallback
- `filterCanRunPostFork` requires profile/upstream fork match (no blanket manifest-dir pass)
- `determineBaseProfile` prefers exact post-fork profile match before manifest dir default

### Modified `bcos-evm/test/eth-eest-test/src/EestGranularCli.cpp` + header

- Added `buildRunnerConfig(profileIds)` with manifest-16 default: eth-shanghai, eth-cancun, eth-prague, eth-osaka

### Modified `bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp`

- Replaced triple-profile brute scan with `resolveRunsForCase` + `tryListSubtests(tc, run.postForkKey)`
- Uses `run.executionProfile` from `resolveExecutionProfile`
- assertLevels aligned to manifest: `transitional`, `expectException`, `stateRoot` (dropped `logsHash`)
- Per-case empty runs → `continue` (not file-level skip)
- `stateTestsRoot` wired via `resolveEestRoot()/fixtures/state_tests`

### Created `bcos-evm/test/eth-eest-test/test/EestForkInferenceTest.cpp`

- 6 unit tests: manifest map (7623, 7702, cancun), path inference, post-fork key resolution, non-matching fork skip

### Modified `bcos-evm/test/eth-eest-test/CMakeLists.txt`

- Added `EestForkInference.cpp` to `bcos-evm-specs-tests-core`
- Added `add_reference_test(EestForkInferenceTest)`

## Verification

```bash
cmake --build build-bcos-evm-check --target EestForkInferenceTest EthEestStateGranular EthExecutionSpecStateTests -j$(sysctl -n hw.ncpu)

# Unit tests
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EestForkInferenceTest        # 6 cases, no errors
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/ForkProfileRegistryTest      # 7 cases, no errors
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EestGranularCliTest          # 1 case, no errors

# M8 granular integration
GRAN=build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthEestStateGranular
ROOT=build-bcos-evm-check/_deps/evm_ref_eest_root/fixtures/state_tests
$GRAN $ROOT/cancun/eip4844_blobs --fork-profiles eth-cancun --gtest_filter='*' --gtest_brief=1
# [  PASSED  ] 22 tests.

$GRAN $ROOT/prague/eip7623_increase_calldata_cost --fork-profiles eth-osaka --gtest_filter='*' --gtest_brief=1
# [  PASSED  ] 8 tests.

# Manifest regression (unchanged)
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthExecutionSpecStateTests \
  --manifest bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json \
  --eest-root build-bcos-evm-check/_deps/evm_ref_eest_root \
  --expectations bcos-evm/test/eth-eest-test/manifests/expectations.json
# All 4140 EEST state subtest(s) passed

ctest -L specs-tests-smoke -R 'EthEestStateGranularSmoke|ForkProfileRegistryTest|EestForkInferenceTest|EestGranularCliTest' --test-dir build-bcos-evm-check
# 4/4 passed
```

## Test Summary

| Target | Result |
|--------|--------|
| `EestForkInferenceTest` | 6 cases, no errors |
| `ForkProfileRegistryTest` | 7 cases, no errors |
| `EestGranularCliTest` | 1 case, no errors |
| `EthEestStateGranularSmoke` | PASS |
| Granular `cancun/eip4844_blobs` + `eth-cancun` | 22/22 |
| Granular `prague/eip7623` + `eth-osaka` | 8/8 |
| `EthExecutionSpecStateTests` manifest full | **4140/0** |

## Self-Review

| Check | Result |
|-------|--------|
| Harness-only scope (`test/eth-eest-test/`) | PASS |
| `tryListSubtests(tc, run.postForkKey)` not `upstreamForkName` | PASS |
| assertLevels without `logsHash` | PASS |
| Manifest prague/eip7623 & eip7702 → eth-osaka | PASS |
| Default profiles manifest-16 (no Berlin/London/Paris) | PASS |
| Manifest 4140/0 baseline untouched | PASS |

## Concerns

1. **Brief vs implementation ordering:** Brief Step 2 lists manifest lookup before post-fork match; implementation prefers exact post-fork filter match first to avoid running Shanghai/Osaka variants under `--fork-profiles eth-cancun` on multi-fork JSON files. Manifest path overrides (prague/eip7623, eip7702) still force eth-osaka.
2. **File-level granularity:** Directory mode registers one GTest per JSON file; any subtest failure fails the file. Subtest-level mode (single-file arg) gives finer isolation — unchanged from prior design.
3. **Default 4-profile filter on multi-fork dirs:** Running with no `--fork-profiles` will execute all post forks matching shanghai/cancun/prague/osaka keys; use explicit `--fork-profiles` for single-fork slices (matches manifest runner semantics per entry).
