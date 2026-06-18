# Task C3-2 Report — executeViaHost orchestration layer

## Status

- Implemented `executeViaHost` orchestration skeleton in `bcos-evm/bcos/ExecuteViaHost.h/.cpp`.
- Added `FiscoTxAdapter` (`deriveMessage`, CR-A) and `FiscoTransactionPrepare` thin wrapper (Prep-A).
- Added smoke test `bcos-evm/test/ExecuteViaHostSmokeTest.cpp` and registered `ExecuteViaHostSmoke` in CTest.
- Kept `TransactionExecutorImpl` untouched (C5 guardrail respected).

## Implemented

- Added `bcos-evm/bcos/ExecuteViaHost.h/.cpp`
  - Orchestration order:
    - `deriveMessage` (CREATE / CREATE2 address resolution),
    - warm-set prepare (`prepareTransaction` -> `warmTransactionEntry`),
    - optional auth gate callback (`authChecker`),
    - EIP-7623 calldata pre-debit,
    - optional host-side value transfer pre-step,
    - transfer-gas consumption,
    - `State` checkpoint + `EthHost` + `FiscoHostExtension` execution,
    - logs capture to `FiscoExecutionContext.logs` before state discard,
    - SUCCESS -> `StateDiff` output, failure -> state revert + `fix_revert_logs` gate.
  - Added catch-table style exception mapping with `fix_error_handling` gate for OOG / insufficient balance / internal errors.
- Added `bcos-evm/bcos/FiscoTxAdapter.h`
  - New `deriveMessage(const FiscoTxAdapterInput&)` for CR-A behavior (CREATE/CREATE2 target prefill).
- Added `bcos-evm/bcos/FiscoTransactionPrepare.h`
  - Thin prepare wrapper over `warmTransactionEntry` for Prep-A integration point.
- Updated `bcos-evm/eth/state/EthHost.hpp/.cpp`
  - Added log collection in `emit_log()`,
  - Added `take_logs()` so orchestration can transfer host logs into execution context.
- Updated `bcos-evm/CMakeLists.txt`
  - Added `bcos/ExecuteViaHost.cpp` to `bcos-evm` static library.
- Added `bcos-evm/test/ExecuteViaHostSmokeTest.cpp`
  - Happy-path smoke: empty-account CALL via `executeViaHost` returns `EVMC_SUCCESS`.
- Updated `bcos-evm/test/CMakeLists.txt`
  - Added `ExecuteViaHostSmokeTest` executable and `ExecuteViaHostSmoke` ctest registration.

## Verification

Executed in this workspace:

- `cmake --build build --target bcos-evm`
- `ctest --test-dir build/bcos-evm/test`

Result: all registered `bcos-evm` tests pass, including `ExecuteViaHostSmoke`.

