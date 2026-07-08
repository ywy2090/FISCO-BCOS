# Task C2-1 — warmTransactionEntry + BlockInfoBuilder

## Files (plan)
- Create: `bcos-evm/eth/execution/warmTransactionEntry.h`
- Create: `bcos-evm/eth/execution/BlockInfoBuilder.h`
- Test: `bcos-evm/test/state/WarmTransactionEntryTest.cpp` (add to bcos-evm/test/CMakeLists.txt)

## Requirements

### warmTransactionEntry (Grill E-C)
Warm at tx entry before EVM execute:
- sender (tx.from / origin)
- callee (tx.to if CALL)
- access_list entries (EIP-2930 W2)
- coinbase (block.coinbase, EIP-3651+)

Use **new** `bcos::evm::state::State` warm APIs (`warm_up_address`, `warm_up_storage`) — NOT legacy `eth/eip2929/*`.

Signature suggestion:
```cpp
void warmTransactionEntry(state::State& state, evmc_revision rev,
    const state::Transaction& tx, const state::BlockInfo& block,
    const state::TransactionProperties& props,
    const Eip2930AccessList* accessList = nullptr);
```

Reference semantics from `bcos-evm/eth/eip2929/Eip2929TransactionPrewarm.h` but implement against State.

### BlockInfoBuilder
Helper to build `state::BlockInfo` from test/minimal inputs and eventually Fisco block context fields (coinbase, timestamp, chainId, gasPrice, prevRandao, baseFee).

### Tests (TDD)
- warmTransactionEntry marks sender, to, coinbase warm on State
- access list warms address + storage keys
- BlockInfoBuilder produces expected BlockInfo fields

## Constraints
- No vendor evmone test/state
- No `#include "bcos-executor` in new eth/execution files — use `bcos-evm/eth/AccessList.h`
- CMake: only add new files if .cpp needed (headers-only OK)
- `cmake --build build --target bcos-evm` + new test target PASS

Report: sdd/task-C2-1-report.md
