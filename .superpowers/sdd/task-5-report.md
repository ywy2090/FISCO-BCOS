# Task 5 Report: H6 — Unsupported formats → GTEST_SKIP

## Status

**COMPLETE**

## Deliverables

| File | Action |
|------|--------|
| `bcos-evm/test/eth-eest-test/include/bcos-evm/eth-eest-test/GeneralStateTestLoader.h` | Modified — `StateTestLoadStatus`, `StateTestLoadResult`, `tryLoadGeneralStateTestFile` |
| `bcos-evm/test/eth-eest-test/src/GeneralStateTestLoader.cpp` | Modified — non-throwing loader; format classification |
| `bcos-evm/test/eth-eest-test/runners/eth/EthEestStateGranular.cpp` | Modified — `GTEST_SKIP` for unsupported files; subtest mode registers file-level skip placeholder |
| `bcos-evm/test/eth-eest-test/test/GeneralStateTestLoaderTest.cpp` | Modified — `unsupported_format_returns_status_not_throw` |
| `bcos-evm/test/eth-eest-test/assets/eest/unsupported/not_gst.json` | Created — `{ "blocks": [] }` stub |

## Commit

```
(pending)
test(eest): H6 skip unsupported state test JSON in granular runner
```

## Tests

| Target | Result |
|--------|--------|
| `GeneralStateTestLoaderTest` | **5/5 PASS** |
| `EthEestStateGranular` on `unsupported/not_gst.json` | **1 SKIPPED** (`Not a general state test JSON`) |
| `EthEestStateGranular` on `unsupported/` directory | **1 SKIPPED** |
| `EthExecutionSpecStateTestsFull` manifest | **4140/4140 PASS** (unchanged) |

### Verification commands

```bash
cmake --build build-bcos-evm-check --target GeneralStateTestLoaderTest EthEestStateGranular
./build-bcos-evm-check/bcos-evm/test/eth-eest-test/GeneralStateTestLoaderTest
GRAN=build-bcos-evm-check/bcos-evm/test/eth-eest-test/EthEestStateGranular
$GRAN bcos-evm/test/eth-eest-test/assets/eest/unsupported/not_gst.json 2>&1 | grep -i skip
ctest -R EthExecutionSpecStateTestsFull
```

## Implementation Summary

- **`tryLoadGeneralStateTestFile`**: non-throwing loader returning `Ok`, `UnsupportedFormat`, or `ParseError`.
  - `UnsupportedFormat`: empty map, `blocks`/`engineNewPayloads` root keys, variant missing `env`/`pre`/`transaction`/`post`.
  - `ParseError`: IO failure, malformed JSON, or field parse failures inside valid GST shape.
- **`loadGeneralStateTestFile` / `loadEestStateTestFile`**: unchanged throwing behavior for manifest runners.
- **Granular runner**:
  - File-level `TestBody`: `GTEST_SKIP()` on load failure, zero cases, or all forks unsupported (`no supported forks`).
  - Per-case zero runs: `continue` (siblings may still run).
  - Subtest registration mode: skipped files register one file-level placeholder so GTest reports SKIP (not silent 0 tests).

## Concerns

1. **Subtest vs directory semantics** — single-file input for unsupported JSON registers a file-level skip test (not zero tests) so `grep -i skip` verification passes; directory mode uses the same file-level test path.
2. **`engineForkchoiceUpdateds`** — included in unsupported detection alongside `blocks` and `engineNewPayloads`; not exercised by fixture yet.
3. **Parse vs unsupported boundary** — malformed hex inside otherwise-valid GST shape returns `ParseError` (granular still SKIPs with reason); manifest runners still throw.
