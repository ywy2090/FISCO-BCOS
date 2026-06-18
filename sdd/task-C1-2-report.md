# Task C1-2 Report — FISCO sstoreStatus in EthHost::set_storage

## Status
- Implemented Task C1-2 in `EthHost::set_storage` with FISCO-compatible `sstoreStatus` behavior.
- Added TDD coverage in `SstoreStatusTest` and wired it into `ctest`.
- Verified the target test passes.

## Implemented
- Updated `bcos-evm/eth/state/EthHost.hpp`
  - Added `fixStorageStatus` constructor parameter (default `true`).
  - Added per-slot original-value cache for status classification.
- Updated `bcos-evm/eth/state/EthHost.cpp`
  - `set_storage()` now:
    - caches original value on first write of each `(address, key)` slot,
    - applies write via `State::set_storage`,
    - returns status from FISCO-compatible classifier.
  - `classifyStorageStatus()` now supports:
    - **fix ON**: 4-state (`ASSIGNED/ADDED/DELETED/MODIFIED`),
    - **fix OFF**: 2-state (`DELETED/MODIFIED`).
  - This disables the evmone 9-state-style path in `EthHost` for this flow.
- Added `bcos-evm/test/state/SstoreStatusTest.cpp`
  - 4×2 matrix: `existingZero/newZero × fix ON/OFF`.
  - Added an extra regression case asserting status uses original committed value across repeated writes in one host frame.
- Updated `bcos-evm/test/CMakeLists.txt`
  - Added `SstoreStatusTest` target and `SstoreStatus` ctest entry.

## TDD Notes
- RED: `SstoreStatusTest` target initially missing before CMake reconfigure.
- RED: first compile failed due direct `evmc_bytes32` equality operator usage in test.
- GREEN: switched to `Bytes32Equal{}` assertion and tests passed.

## Verification
- PASS: `cmake --build build --target SstoreStatusTest`
- PASS: `ctest --test-dir build/bcos-evm/test -R SstoreStatus`

## Files Changed (C1-2 scope)
- `bcos-evm/eth/state/EthHost.hpp`
- `bcos-evm/eth/state/EthHost.cpp`
- `bcos-evm/test/state/SstoreStatusTest.cpp`
- `bcos-evm/test/CMakeLists.txt`
- `sdd/task-C1-2-report.md`
