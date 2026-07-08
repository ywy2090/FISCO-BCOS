# Task C2-2 — Prague/Osaka eth 向量门禁

## Files
- `bcos-evm/test/state/PragueStateTest.cpp`
- `bcos-evm/test/fixtures/state/` (minimal JSON vectors)
- Update `bcos-evm/test/CMakeLists.txt`

## Step 0 — Fixture selection (document in report)
Pick 5 minimal Prague cases covering:
- CALL to empty account
- CREATE
- SELFDESTRUCT (or delegate if SD unavailable in skeleton)

If full geth JSON unavailable offline, **author minimal inline fixtures** that exercise transition() paths with known expected status/gas/output. Document case names in report.

## Acceptance (Q20)
Compare: status, gas_used, logs, output vs expected in fixture.
**Do NOT** compare state/receipt/tx roots.

## Implementation
- Use `transition()` + `InMemoryStateView` + `evmc::VM{evmc_create_evmone()}`
- Use `BlockInfoBuilder`, `warmTransactionEntry` from C2-1
- Osaka: defer wave 2 with explicit note in report if not feasible

## Tests
- Register `PragueStateTest` in CMake
- `ctest -R PragueState` PASS

## Constraints
- No vendor evmone test/state source into production build
- Fixtures in test/fixtures only
- No bcos-executor includes in new test code

Report: sdd/task-C2-2-report.md
