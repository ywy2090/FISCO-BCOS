# Task C2-2 Report — Prague eth state vector gate

## Status
- Implemented Prague eth-vector gate test `PragueStateTest` on top of `transition()` + `InMemoryStateView` + `evmone`.
- Added 5 minimal JSON fixtures under `bcos-evm/test/fixtures/state/`.
- Registered CTest entry `PragueState`.
- Ran and passed: `ctest --test-dir build/bcos-evm/test -R Prague`.

## Implemented
- Added `bcos-evm/test/state/PragueStateTest.cpp`
  - Loads JSON fixture cases from `test/fixtures/state`.
  - Builds pre-state accounts in `InMemoryStateView`.
  - Executes `transition()` with `EVMC_PRAGUE` and `evmc::VM{evmc_create_evmone()}`.
  - Uses `BlockInfoBuilder` for block context.
  - Compares `status`, `gas_used`, `logs`, `output` only (Q20), without state/receipt/tx roots.
- Added fixtures:
  - `prague_call_empty_account.json`
  - `prague_call_return_word.json`
  - `prague_call_revert.json`
  - `prague_create_empty_initcode.json`
  - `prague_selfdestruct.json`
- Updated `bcos-evm/test/CMakeLists.txt`
  - Added `PragueStateTest` target and `PragueState` ctest registration.
  - Added fixture directory compile definition for runtime loading.

## Fixture Selection (Step 0)
Selected minimal Prague fixtures (5 cases):
- CALL to empty account.
- CALL returning one 32-byte word.
- CALL REVERT.
- CREATE with empty initcode.
- SELFDESTRUCT to beneficiary.

These cover required categories (`CALL`, `CREATE`, `SELFDESTRUCT`) with deterministic expected values for `status/gas/logs/output`.

## Acceptance Check (Q20)
- Compared:
  - `status`
  - `gas_used`
  - `logs` count
  - `output`
- Not compared:
  - state root
  - receipt root
  - tx root

## Osaka Note
- Osaka vectors are deferred to wave 2 for this task, as permitted by the brief.
- Current gate scope is Prague only.

## Build / Test Summary
- Passed:
  - `ctest --test-dir build/bcos-evm/test -R Prague`
    - `PragueState` PASS.
