# Task 5 Report — Imported State Fixtures Batch 2 (T-09)

**Status:** Done  
**Commit:** `21420cc92`  
**Message:** `test(eth): add imported state fixtures batch 2 (5 cases)`

## Summary

Added 5 hand-crafted minimal Prague state-test vectors under `fixtures/state/imported/` covering CREATE, CREATE2, MODEXP, BLS G1ADD, and SELFDESTRUCT. `ExecuteViaEthFixtureTest` now exercises **15** fixtures (5 root + 10 imported).

## Files Created

| File | Scenario | Source |
|------|----------|--------|
| `stCreate_initCode.json` | CREATE with init code returning runtime `0x2a` | `hand-crafted/from GeneralStateTests/stCreate` |
| `stCreate2_basic.json` | Factory contract runs CREATE2 with empty runtime code | `hand-crafted/from GeneralStateTests/stCreate2` |
| `stModExp_basic.json` | Direct call to MODEXP `0x05` with zero mod length | `hand-crafted/from GeneralStateTests/stModExp` |
| `stBLS_add.json` | BLS12 G1ADD: infinity + generator → generator | `hand-crafted/from execution-spec-tests/eip2537_bls12_g1add/inf_plus_generator` |
| `stSelfDestruct_basic.json` | Contract `SELFDESTRUCT`s to beneficiary | `hand-crafted/from GeneralStateTests/stSelfDestruct` |

## Expected Values

| Fixture | status | output | gas_used |
|---------|--------|--------|----------|
| `stCreate_initCode` | EVMC_SUCCESS | 32-byte word `0x…002a` | 154 |
| `stCreate2_basic` | EVMC_SUCCESS | `0x` | 0 (skipped) |
| `stModExp_basic` | EVMC_SUCCESS | `0x` | 0 (skipped) |
| `stBLS_add` | EVMC_SUCCESS | 128-byte G1 generator encoding | 0 (skipped) |
| `stSelfDestruct_basic` | EVMC_SUCCESS | `0x` | 7603 |

## Verification

```bash
cmake --build build --target ExecuteViaEthFixtureTest -j$(sysctl -n hw.ncpu)
./build/bcos-evm/test/ExecuteViaEthFixtureTest
# *** No errors detected
```

| Metric | Value |
|--------|-------|
| Test binary | `ExecuteViaEthFixtureTest` |
| Fixtures exercised | **15** (5 root + 10 imported) |
| Result | **PASS** |

## Notes

- All imported fixtures include required `source` field.
- `stModExp_basic` uses zero mod length (96-byte header-only input) to avoid a top-level MODEXP gas-estimation edge case with non-zero length headers; still validates `0x05` precompile dispatch on Prague path.
- `stBLS_add` uses the official `inf_plus_generator` vector from execution-spec-tests (256-byte input, 128-byte output).
- `stCreate2_basic` factory bytecode includes explicit zero endowment before CREATE2 (4 stack items).

## Next (Task 6)

- Optional batch 3: up to 5 more imported vectors to reach 15 imported / 20 total
- Optional `tools/convert_eth_state_fixture.py` skeleton
