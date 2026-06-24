# Task 5 Report — ExecutionFrame Nested Production Path

**Base:** dec56b4a5  
**Status:** ✅ Complete  
**Date:** 2026-06-24

## Summary

Implemented full `runExecutionFrame(Nested)` pipeline in `ExecutionFrame.cpp`, delegating `EthHost::call` to it. Removed duplicated routing/transfer/caller helpers from `EthHost`. Fixed six vendored-`EthHost.cpp` state test targets to link `bcos-evm-eth`.

## Changes

| File | Change |
|------|--------|
| `bcos-evm/eth/execution/ExecutionFrame.cpp` | Full RR6 nested pipeline: route → DELEGATECALL guard → precompile → caller/prepare → CREATE bind → checkpoint → init → transfer → VM → finalize (incl. §4.1 CREATE nonce bump) |
| `bcos-evm/eth/state/EthHost.cpp` | Thin `call()` delegating to `runExecutionFrame(Nested)`; deleted `routeCall`, `resolveExecutionCode`, `transferValue`, `resolveCallerAddress` |
| `bcos-evm/eth/state/EthHost.hpp` | Removed `RoutedCall` and moved helper declarations |
| `bcos-evm/test/cmake/StateTests.cmake` | 6 targets now link `bcos-evm-eth` instead of vendoring eth sources |

## RR6 Order (verified)

1. `routeMessage(Nested)`
2. DELEGATECALL→precompile guard
3. `dispatchPrecompile` (early return + `precompileHit`/`gasRefund`)
4. `resolveCallerAddress` + `setCallerAddress` + `prepareMessage`
5. `bindCreateMessageForInit` (CREATE)
6. `checkpoint`
7. `initializeCreateTargetAccount` (CREATE)
8. `transferFrameValue(Nested)`
9. `resolveExecutionCode` + `vm.execute`
10. Finalize: code deposit, install code, `markCreatedInTx`, commit/revert, execution address update, nested CREATE nonce bump

## Global Constraints

- ✅ `eth/execution/` has no bcos/opstack includes
- ✅ `executeMessage.cpp` unchanged by this task
- ✅ Live `dispatchPrecompile` call sites: `ExecutionFrame.cpp` (nested/EthHost path) + `ExecuteMessage.cpp` (top-level, pre-existing)

## Test Results

```
ctest -R "^PrecompileRouterEnvelope$"           → 1/1 PASS
ctest -R "^PrecompileRouter"                    → 4/4 PASS
ctest -R "^PrecompileRouterCharacterization$|^PrecompileRouterEquivalence$" → 2/2 PASS
cmake --build build --target NestedCallHostTest PragueStateTest → PASS

Additional state targets (cmake fix verification):
  PragueState, NestedCallHost, PrecompileInCall, BlockHashHost, NestedRevertWarm, EvmoneRefundSpike → 6/6 PASS
```

## Concerns / Follow-ups

1. **TopLevel stub:** `runExecutionFrame` returns `EVMC_INTERNAL_ERROR` for non-Nested scope; Task 6+ should implement TopLevel before wiring `executeMessage`.
2. **gasRefund (RR4):** `EthHost::call` intentionally ignores `fr.gasRefund`; nested precompile refund propagation remains a later orchestration concern.
3. **Pre-existing dirty tree:** Many unrelated modified/untracked files remain outside this commit scope.

## Commit

```
feat(bcos-evm): Delegate EthHost::call to runExecutionFrame(Nested)
```
