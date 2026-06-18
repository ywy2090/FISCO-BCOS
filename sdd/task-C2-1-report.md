# Task C2-1 Report — warmTransactionEntry + BlockInfoBuilder

## Status
- Implemented `eth` vector path transaction-entry warming helper and block-info builder.
- Added TDD test suite `WarmTransactionEntryTest` and wired it into `bcos-evm/test/CMakeLists.txt`.
- `bcos-evm` target builds successfully.

## Implemented
- Added `bcos-evm/eth/execution/warmTransactionEntry.h`
  - Warm sender (`tx.from`)
  - Warm callee (`tx.to`) for CALL path when `props.warmDestination` is enabled
  - Warm coinbase when `props.warmCoinbase` and `rev >= EVMC_SHANGHAI`
  - Warm EIP-2930 access-list address + storage keys via `state::State` APIs:
    - `warm_up_address`
    - `warm_up_storage`
- Added `bcos-evm/eth/execution/BlockInfoBuilder.h`
  - `BlockInfoFields` + `buildBlockInfo()`
  - fluent `BlockInfoBuilder` for number/timestamp/gasLimit/coinbase/prevRandao/baseFee/chainId/blobBaseFee/gasPrice
- Updated `bcos-evm/eth/state/transition.cpp`
  - replaced inline warm-up logic with `execution::warmTransactionEntry(...)`
- Added tests in `bcos-evm/test/state/WarmTransactionEntryTest.cpp`
  - sender/to/coinbase warm assertions
  - access-list address + storage warm assertions
  - `BlockInfoBuilder` field construction assertions
- Updated `bcos-evm/test/CMakeLists.txt`
  - added `WarmTransactionEntryTest` target and `ctest` entry

## TDD Notes
- RED observed first: build failed because new execution headers were not present.
- GREEN implementation added after RED signal.

## Build / Test Summary
- Passed:
  - `cmake --build build --target bcos-evm`
- Blocked in this workspace:
  - `cmake --build build --target WarmTransactionEntryTest`
  - Linker error: `ld: library 'bcos-ledger' not found`
  - This is an existing workspace link-resolution issue (`-lbcos-ledger`) unrelated to C2-1 logic implementation.
