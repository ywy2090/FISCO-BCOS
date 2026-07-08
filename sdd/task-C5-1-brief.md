# Task C5-1 — TransactionExecutorImpl 接线

## Files
- Modify: `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`
- Modify: `bcos-evm/bcos/FiscoTxExecutor.h` (and OpStackTxExecutor.h if needed)
- Possibly delete: `transaction-executor/bcos-transaction-executor/vm/ExecuteFrame.h` (forward to deleted file)
- Modify: `transaction-executor/tests/CMakeLists.txt` if needed

## Step 1 — Data struct migration
- Replace `hostcontext::ExecuteFrame m_hostContext` → `FiscoExecutionContext m_executionContext`
- Remove `TransientStorage m_transientStorage` + `m_rollbackableTransientStorage` (T-A: transient in State)
- Remove `m_warmsetAccess` if superseded by State journal warm set
- Keep: rollbackable storage, savepoints, buyGas savepoint, policy, nonce, gas limit, evmcResult

## Step 2 — Prepare phase (Prep-A)
- Build `FiscoStateView` over `m_rollbackableStorage`
- Build ephemeral `state::State` + call `prepareTransaction()` / warmTransactionEntry
- Touch ReadWriteSetStorage read set via FiscoStateView syncWait reads (existing pattern)

## Step 3 — Execute phase
Order (spec §8, retain updateNonce before execute):
```
updateNonce()  // keep existing Web3 path
if (!call && fix_gas_precheck) buyGas()
build ExecuteViaHostInput from tx/block/policy
co_await executeViaHost(input)
if SUCCESS: co_await applyStateDiff(storage, output.stateDiff, ...)
if !call && fix_gas_precheck: refundGas()
else if !call && !fix_gas_precheck: consumeBalance()
settleGasUsedFromEvmResult from executionContext.gasSettlementSnapshot
```

Wire:
- `FiscoHostExtension` with deps (storage, blockHeader, ledgerConfig, precompiledManager, contextID, seq)
- `authChecker` → checkAuth from AuthCheck.h
- `precompileCaller` → PrecompiledManager dispatch
- `createAuthTableInvoker` → createAuthTable via syncWait
- VM: use evmone (evmc_create_evmone) — store in executor or create per-tx

## Step 4 — FiscoTxExecutor refactor
Replace `data.m_hostContext.*` with `data.m_executionContext.*`:
- `message()` → `m_executionContext.message`
- `revisionConfig()` → `m_executionContext.revisionConfig`
- `logs()` → `m_executionContext.logs`
- `gasSettlementSnapshot()` → `m_executionContext.gasSettlementSnapshot`

Replace `getAccount(data.m_hostContext, addr)` with:
```cpp
ledger::account::EVMAccount account(data.m_rollbackableStorage, addr, data.m_executionContext.revisionConfig.use_raw_address)
```

## Step 5 — Finalize
- `makeReceipt` uses `m_executionContext` + `fix_revert_logs` gate (unchanged semantics)

## Step 6 — Tests
- `ctest --test-dir build/bcos-evm/test` — must stay 8/8
- `ctest --test-dir build-c3-3 -R ExecuteViaHostCompat` or rebuild compat target
- Scheduler (if buildable): `ctest -R 'FIB101_102_103_104_SchedulerTest'` — document if blocked by WIP

## Constraints
- Do NOT reintroduce bcos-evm/eth/vm/HostContext.h
- Scope-A: bcos-executor HostContext untouched
- Full transaction-executor lib build is goal; document blockers

Report: sdd/task-C5-1-report.md
