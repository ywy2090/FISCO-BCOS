# Task C1-2 — EthHost::set_storage FISCO sstoreStatus

## Files
- Modify: `bcos-evm/eth/state/EthHost.hpp/.cpp`
- Test: `bcos-evm/test/state/SstoreStatusTest.cpp` + CMakeLists.txt

## Requirements (spec §20.2)
- Replicate `HostContext::sstoreStatus` from `bcos-evm/eth/vm/HostContext.h` (~871-893)
- `fix_storage_status` ON → 4-state EIP-2200 semantics (newZero/existZero variants)
- `fix_storage_status` OFF → 2-state simplified
- DIRECT read bypasses journal (for status classification only)
- **Disable** evmone 9-state path in EthHost for FISCO path — use extension flag or HostExtension hook

## Implementation approach
- Add `bool fixStorageStatus` to EthHost constructor or read from HostExtension
- For eth vector path (EthHostExtension default): keep standard `classifyStorageStatus` (EIP-2200 4-state) — C1-2 is FISCO 4态/2态 which differs when flag OFF
- Read HostContext::sstoreStatus logic carefully

## Tests (TDD)
- 4×2 matrix: newZero/existZero × flag ON/OFF
- Register in bcos-evm/test/CMakeLists.txt as SstoreStatusTest

## Constraints
- No bcos-executor includes
- Only touch EthHost + test files

Report: sdd/task-C1-2-report.md
