# Task C3-1 Report — FiscoHostExtension + FiscoExecutionContext

## Status

- Implemented `FiscoHostExtension` under `bcos-evm/bcos/` only, with Hook#8 entry callback support.
- Added `FiscoExecutionContext` struct for C3 orchestration data handoff.
- Added `bcos-evm/test/` unit tests for default policy, `0x1000+` precompile dispatch, and CREATE-frame journal rollback behavior.

## Implemented

- Added `bcos-evm/bcos/FiscoHostExtension.h/.cpp`
  - Extends `state::HostExtension` with C3-1 defaults:
    - `allowSelfdestruct() -> false`
    - `allowDelegateCallToPrecompile() -> false`
    - `skipHostValueTransfer() -> true` when balance transfer is enabled in constructor.
  - Implements `callFiscoPrecompile()` with FISCO precompile address gate (`0x1000+`) and injected callback delegation.
  - Implements `onCreateFrameEntry()` hook via injected CREATE-frame handler.
  - Exposes callback injection points for C3 orchestration:
    - `FiscoPrecompileCaller` for `0x1000+` precompile delegation,
    - `CreateFrameEntryHandler` for Hook#8 CREATE-frame side effects (`createAuthTable` + nonce semantics in follow-up wiring).
- Added `bcos-evm/bcos/FiscoExecutionContext.h`
  - `evmc_message message`
  - `RevisionConfig revisionConfig`
  - `std::vector<protocol::LogEntry> logs`
  - `gas::TxGasSettlementContext gasSettlementSnapshot`
- Updated `bcos-evm/CMakeLists.txt`
  - linked in `bcos/FiscoHostExtension.cpp`.
- Added `bcos-evm/test/FiscoHostExtensionTest.cpp`
  - verifies default FISCO policy switches,
  - verifies `callFiscoPrecompile()` delegates for `0x1000+` target,
  - verifies CREATE-frame hook writes are reverted by `state::State` checkpoint/revert (journal domain).
- Updated `bcos-evm/test/CMakeLists.txt`
  - added `FiscoHostExtensionTest` target and `FiscoHostExtension` ctest entry.

## Out of Scope / Guardrails

- Did not implement `executeViaHost` (C3-2).
- Did not switch `TransactionExecutorImpl` (C5).
- Did not touch `bcos-evm/test/state/Prague*`.

## Verification

- Executed in this workspace:
  - `cmake --build build --target bcos-evm`
  - `cmake --build build --target FiscoHostExtensionTest`
  - `ctest --test-dir build/bcos-evm/test -R FiscoHostExtension`

## 2026-06-18 Review Fix Follow-up

- Refactored constructor inputs to `FiscoHostExtensionDeps` and kept required Hook#8 context as retained members:
  - `storageRef`, `blockHeader`, `ledgerConfig`, `precompiledManager`, `contextID`, `seq`,
    `externalCaller`, `origin`, revision flags, and `state::State*`.
- Implemented `onCreateFrameEntry` semantics aligned to `HostContext` create flow:
  - `blockNumber != 0` triggers auth-table path resolution.
  - FIB-82 path rule: `fix_auth_check && use_raw_address` uses
    `"/apps/" + hex(code_address)`, otherwise uses recipient resolver path.
  - Hook#8 nonce behavior:
    - `web3Tx && createLevel != 0` increments sender nonce in `state::State`.
    - `fix_nonce_init` sets recipient/create-target nonce to `1` before initcode run.
- Clarified balance-transfer flag naming:
  - replaced `enableBalanceTransfer` with `skipEvmNativeValueTransfer` in extension constructor
    and tests.
- Extended `FiscoHostExtensionTest`:
  - asserts auth-table path callback invocation with FIB-82 raw-address path behavior,
  - asserts nested CREATE sender nonce increment (`web3Tx + level != 0`),
  - keeps journal revert behavior test for CREATE-frame side effects,
  - adds `<0x1000` precompile dispatch negative test (`nullopt`).
- Added Apache license headers to new files:
  - `bcos-evm/bcos/FiscoHostExtension.h`
  - `bcos-evm/bcos/FiscoHostExtension.cpp`
  - `bcos-evm/bcos/FiscoExecutionContext.h`
- Left `PragueStateTest` CMake placement unchanged in this follow-up (C2-2 scope guardrail).

### Follow-up Verification

- `cmake --build build --target bcos-evm FiscoHostExtensionTest` ✅
- `ctest --test-dir build/bcos-evm/test -R FiscoHostExtension` ✅
