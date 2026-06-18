# Task C1-1 Fix Report

## Status

- Completed: relocated FISCO state adapters from `transaction-executor/bcos-transaction-executor/state/` to `bcos-evm/bcos/`.
- Completed: removed wrong-path adapter headers from `transaction-executor`.
- Completed: updated `transaction-executor/tests/FiscoStateViewTest.cpp` to include adapters from `bcos-evm/bcos/`.
- Completed: removed `bcos-executor/src/Common.h` dependency from relocated adapters and used `bcos-evm/eth/state/hash_utils.hpp`.
- Completed: ABI storage wiring for C1-1
  - `Account` now carries `abi`.
  - `FiscoStateView` reads ABI from `EVMAccount::abi()`.
  - `applyStateDiff()` passes ABI through `EVMAccount::setCode(...)`.
- Completed: `FiscoBlockInfo` now fills `coinbase` from `BlockHeader::extraData()` when present, with zero default fallback.

## Files Changed (C1-1 fix scope)

- Added:
  - `bcos-evm/bcos/FiscoStateView.h`
  - `bcos-evm/bcos/FiscoStateView.cpp`
  - `bcos-evm/bcos/FiscoBlockInfo.h`
  - `bcos-evm/bcos/StateDiffApplier.h`
  - `sdd/task-C1-1-fix-report.md`
- Deleted:
  - `transaction-executor/bcos-transaction-executor/state/FiscoStateView.h`
  - `transaction-executor/bcos-transaction-executor/state/StateDiffApplier.h`
  - `transaction-executor/bcos-transaction-executor/state/FiscoBlockInfo.h`
- Updated:
  - `bcos-evm/CMakeLists.txt`
  - `bcos-evm/eth/state/Account.hpp`
  - `bcos-evm/eth/state/hash_utils.hpp`
  - `transaction-executor/tests/FiscoStateViewTest.cpp`

## Verification

### Build

- PASS: `cmake --build build --target bcos-evm`

### Test target compile

- BLOCKED by existing unrelated tree state: `cmake --build build --target test-transaction-executor`
- Failure reason observed in current workspace:
  - `bcos-evm/eth/AccessList.h` currently fails type resolution (`Address` undeclared), which breaks `bcos-executor` before `FiscoStateViewTest` can be compiled.

## Notes

- Constraint respected: no edits were made under `bcos-evm/eth/execution/` (C2-1 parallel track).
