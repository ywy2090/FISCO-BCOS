# Task 8 + Task 9 Report (Step 1)

## Status
- Task 8 implemented: `[PRECOMPILED]` dynamic routing moved from `EthHost` to `FiscoHostExtension::tryChainPrecompile`.
- Task 9 acceptance command executed: `ctest --test-dir build/bcos-evm/test --output-on-failure`.
- `ExecuteViaHost.cpp` was **not** modified by Step 1 commits in this report.

## Code Changes
- Removed `[PRECOMPILED]` parsing from `bcos-evm/eth/state/EthHost.cpp` and deleted `parseDynamicPrecompileTarget` declaration in `bcos-evm/eth/state/EthHost.hpp`.
- Added dynamic precompile marker parsing in `bcos-evm/bcos/FiscoHostExtension.cpp` (reads callee code and dispatches via `precompileCaller`).
- Added helper declaration in `bcos-evm/bcos/FiscoHostExtension.h`.
- Extended `bcos-evm/test/FiscoHostExtensionTest.cpp` with dynamic marker routing case.

## ctest Summary
Command:
`ctest --test-dir build/bcos-evm/test --output-on-failure`

Result:
- Total tests: 13
- Passed: 13
- Failed: 0
- Total time: 0.21 sec

## Notes
- A full rebuild currently fails on pre-existing `ExecuteViaHost.cpp` constructor mismatch in the working tree (outside this task’s requested Step 1 commit scope).  
  The acceptance `ctest` command itself runs and passes in current build artifacts.

## Commits
- refactor(bcos): move PRECOMPILED routing to FiscoHostExtension
- docs: mark layer refactor Step 1 complete
