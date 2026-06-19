# Task 2 Report: EthHost 构造扩展（VM + BlockHashes）

**Status:** ✅ Complete  
**Date:** 2026-06-19

## Summary

Extended `EthHost` constructor to accept `evmc::VM&` and `BlockHashes`, wired `get_block_hash()` to delegate to the callback, and updated `transition.cpp` plus all direct `EthHost` instantiation sites in eth tests.

## Interface

```cpp
EthHost(State& state, evmc_tx_context txContext, evmc_revision revision,
    evmc::VM& vm, BlockHashes blockHashes,
    HostExtension* extension = nullptr, bool fixStorageStatus = true);
```

`BlockHashes` = `std::function<evmc_bytes32(int64_t)>` (from `BlockInfo.hpp`).

## Files Changed

| File | Change |
|------|--------|
| `bcos-evm/eth/state/EthHost.hpp` | Constructor + `m_vm` / `m_blockHashes` members |
| `bcos-evm/eth/state/EthHost.cpp` | Constructor init; `get_block_hash` delegates to callback |
| `bcos-evm/eth/state/transition.cpp` | Pass `vm` + `block_hashes` to `EthHost` |
| `bcos-evm/test/state/Eip2929AccessHostTest.cpp` | VM + stub `BlockHashes` at each site |
| `bcos-evm/test/EthHostExtensionHooksTest.cpp` | VM + `emptyBlockHashes()` helper |
| `bcos-evm/test/FiscoHostExtensionTest.cpp` | VM + `emptyBlockHashes()` helper |
| `bcos-evm/test/state/SstoreStatusTest.cpp` | VM + `emptyBlockHashes()` helper |

**Not changed (deferred):** `bcos-evm/bcos/ExecuteViaHost.cpp` — Step 2 wiring.

## Test Results

With temporary `ExecuteViaHost.cpp` fix applied locally for verification:

```
ctest --test-dir build/bcos-evm/test --output-on-failure
9/9 PASS (StateJournalRevert, EthHostExtensionHooks, FiscoHostExtension,
         ExecuteViaHostSmoke, StateHostSmoke, WarmTransactionEntry,
         Eip2929AccessHost, SstoreStatus, PragueState)
```

`PragueStateTest` builds standalone (links eth sources directly) and passes without `ExecuteViaHost` changes.

**Note:** `bcos-evm` static lib does not compile until `ExecuteViaHost.cpp` is updated (Task Step 2). Tests linking `bcos-evm` require that follow-up.

## Concerns

1. **`m_vm` unused** — stored for Task 4 recursive `call()`; compiler warns `-Wunused-private-field`.
2. **`EthHost` non-copyable/non-movable** — `evmc::VM&` reference member; tests must declare `vm` before `host` in the same scope.
3. **`ExecuteViaHost.cpp` compile break** — intentional per Step 1 boundary; one-line fix ready: `*input.vm, input.blockHashes`.

## Next (Task 3+)

- TDD nested CALL failure tests
- Task 4: recursive `call()` using `m_vm`
- Step 2: wire `ExecuteViaHost.cpp`
