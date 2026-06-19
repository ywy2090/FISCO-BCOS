# Task 3 Report — ExecuteViaEthFixtureTest

**Status:** Done  
**Commit:** `f903542be` (amended from `5b65a82e2`)  
**Message:** `feat(test): add ExecuteViaEthFixtureTest for existing Prague fixtures`

## Summary

Added `ExecuteViaEthFixtureTest` to run all 5 existing Prague root fixtures through `executeViaEth`, validating the Task 1/2 fixture loader, adapter, and assertion helpers end-to-end.

## Files Changed

| File | Change |
|------|--------|
| `bcos-evm/test/eth/ExecuteViaEthFixtureTest.cpp` | New — iterates `listAllFixtureFiles`, builds input via `buildExecuteViaEthInput`, runs `executeViaEth`, asserts with `assertFixtureResult` |
| `bcos-evm/test/CMakeLists.txt` | Added `ExecuteViaEthFixtureTest` target + ctest; added `../eth/Eip7702.cpp` to `PragueStateTest` sources (clean-rebuild linker fix) |

## CMake Notes

- Brief specified `bcos-evm` + `Boost::unit_test_framework`; adjusted to `bcos-evm-eth` (where `executeViaEth` lives) and header-only Boost.Test (matches other bcos-evm tests — `Boost::unit_test_framework` target not available in this build).

## Verification

| Test | Result | Details |
|------|--------|---------|
| `ExecuteViaEthFixtureTest` | **PASS** | 1 test case, 5 fixtures (all root Prague JSON) |
| `PragueStateTest` | **PASS** | Regression after `Eip7702.cpp` source addition |

Build dir: `build/`  
```bash
cmake --build build --target ExecuteViaEthFixtureTest PragueStateTest -j$(sysctl -n hw.ncpu)
./build/bcos-evm/test/ExecuteViaEthFixtureTest   # *** No errors detected
./build/bcos-evm/test/PragueStateTest            # *** No errors detected
```

### Fixtures exercised

1. `prague_call_empty_account`
2. `prague_call_return_word` (gas_used=18 asserted)
3. `prague_call_revert`
4. `prague_create_empty_initcode`
5. `prague_selfdestruct`

## txProps / warm-flags decision

**No fix required.** All 5 fixtures passed without Option A (txProps override) or Option B (expected-value adjustment).

Rationale:
- CALL fixtures: `executeViaEth` sets `warmDestination=true` (via `!isCreateKind`), matching fixture defaults used by `transition()`.
- CREATE fixture: `expected.gas_used=0` skips gas assertion in `assertFixtureResult`.
- `prague_call_return_word` gas (18) matched exactly — no warm-flag mismatch observed.

## Concerns / Notes

1. **`listAllFixtureFiles` vs root-only** — Test uses `listAllFixtureFiles` (includes `imported/` when present). Currently only 5 root fixtures exist; Task 4+ imported fixtures will auto-run here unless filtered.
2. **EIP-7623 gas path** — `executeViaEth` deducts intrinsic gas before EVM; fixtures with non-zero `gas_used` may need tolerance on imported vectors (not needed for current 5).

## Review Fix (scope creep in 5b65a82e2)

**Issue:** Commit accidentally bundled CMake targets for `EthTxInputBuilderTest` and `OpStackTxInputBuilderTest` whose source files were not part of the commit (untracked working-tree leftovers). Those blocks broke `cmake` configure.

**Fix:** Removed the two spurious CMake blocks; kept:
- `ExecuteViaEthFixtureTest` target + ctest
- `../eth/Eip7702.cpp` in `PragueStateTest` sources (linker fix)
- `ExecuteViaEthFixtureTest.cpp` unchanged

**Git:** Amended commit `f903542be` (was `5b65a82e2`; not pushed to any remote).

## Next (Task 4)

- Import additional state-test vectors into `fixtures/state/imported/`
- Extend coverage for EIP-7702, access lists, etc.
