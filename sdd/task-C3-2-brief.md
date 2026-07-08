# Task C3-2 — executeViaHost 编排层

## Files
- Create: `bcos-evm/bcos/ExecuteViaHost.h/.cpp`
- Create: `bcos-evm/bcos/FiscoTxAdapter.h` (`deriveMessage`, CR-A)
- Create: `bcos-evm/bcos/FiscoTransactionPrepare.h` (Prep-A)
- Modify: `bcos-evm/CMakeLists.txt`

## Orchestration order (spec §8)
```cpp
deriveMessage(msg);                              // CR-A
warmTransactionEntry(state, tx, block);          // E-C (C2-1)
checkAuth(...) if needed;                        // Auth-A
buyGas / consumeBalance per L-A;                 // delegate to FiscoTxExecutor hooks or inline snapshot
state.checkpoint();
execute via EthHost + FiscoHostExtension;
on SUCCESS: co_await applyStateDiff(storage, diff);
refundGas();
makeReceipt(ctx) — logs captured before State discard (§19)
```

## Requirements
- Wire `FiscoStateView`, `State`, `EthHost`, `FiscoHostExtension`, `FiscoExecutionContext`
- `deriveMessage`: mirror Policy::deriveMessage / FiscoTxAdapter CR-A (CREATE address prefill)
- `FiscoTransactionPrepare`: DAG Prepare read-set hooks (Prep-A) — thin wrapper, may stub if heavy deps
- Error handling: catch table per §20.1 with `fix_error_handling` gate
- revert logs: capture `ctx.logs` before State discard; `fix_revert_logs` gate
- Do NOT switch `TransactionExecutorImpl` (C5)
- Scope-A: bcos/ may include AuthCheck, ExecutiveWrapper, executor types

## Tests
- Minimal unit/smoke in `bcos-evm/test/ExecuteViaHostSmokeTest.cpp` OR extend existing if harness exists
- At minimum: compile + one happy-path mock storage call
- `ctest -R ExecuteViaHost` if registered

Report: sdd/task-C3-2-report.md
