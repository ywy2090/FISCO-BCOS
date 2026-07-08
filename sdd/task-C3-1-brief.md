# Task C3-1 — FiscoHostExtension + FiscoExecutionContext

## Files
- Create: `bcos-evm/bcos/FiscoHostExtension.h/.cpp` (ONLY location for FiscoHostExtension)
- Create: `bcos-evm/bcos/FiscoExecutionContext.h`
- Modify: `bcos-evm/CMakeLists.txt` (add FiscoHostExtension.cpp)

## FiscoHostExtension (spec §5.3, §7.1)
Extends `HostExtension` with defaults:
- `allowSelfdestruct` → false
- `allowDelegateCallToPrecompile` → false
- `skipHostValueTransfer` → true when balance transfer enabled (constructor param)
- `callFiscoPrecompile` → dispatch 0x1000+ via callback/delegate to PrecompiledManager (inject fn or interface — avoid hard TE include if possible; thin `FiscoPrecompileCaller` callback acceptable)

## onCreateFrameEntry (Hook#8, §21)
Override `onCreateFrameEntry`:
- FISCO CREATE nonce semantics
- `createAuthTable` before initcode (FIB-82 table name)
- Writes must go through State journal (for revert)

Constructor holds refs: storage, blockHeader, ledgerConfig, precompiledManager, contextID, seq, externalCaller — pattern from AuthCheck.h/createAuthTable.

## FiscoExecutionContext (spec §5.4, Log-B)
```cpp
struct FiscoExecutionContext {
    evmc_message message;
    RevisionConfig revisionConfig;  // bcos-evm/eth/RevisionConfig.h
    std::vector<protocol::LogEntry> logs;
    TxGasSettlementContext gasSettlementSnapshot;  // or equivalent from EthTxGasSettlement
};
```

## Tests (bcos-evm/test/)
- `FiscoHostExtensionTest.cpp` or extend existing:
  - CREATE + createAuthTable called (mock storage)
  - CREATE revert → auth table rolled back with State journal (§21.4) — may use InMemoryStateView + mock if full storage heavy

## Constraints
- `FiscoHostExtension` ONLY in bcos-evm/bcos/ (not eth/policy/)
- Scope-A: bcos/ may bind AuthCheck/ExecutiveWrapper
- Do NOT implement executeViaHost (C3-2)
- Do NOT switch TransactionExecutorImpl (C5)

Report: sdd/task-C3-1-report.md
