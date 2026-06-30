# Task 2 Report: Phase 3b — Kernel Canonical Names + TE ADR

**Status:** Complete  
**Start:** `a290c486fd761cbae2da4a152790819a1fab91f8`  
**Date:** 2026-06-30

## Summary

Promoted geth kernel symbols to canonical C++ identifiers in `bcos-evm/eth/`: `stateTransitionExecute` (was `runTxPipeline`) and `innerExecute` (was `executeMessage`). Tier E deprecated inline aliases retained for TE compatibility. Added ADR-031 documenting TE migration schedule.

## Renames Applied

| Legacy (Tier E) | Canonical (ADR-031) | Deprecated alias location |
| --- | --- | --- |
| `runTxPipeline` | `stateTransitionExecute` | `TxPipeline.h` |
| `executeMessage` | `innerExecute` | `ExecuteMessage.h` |

## GethNamingAliases.h

- Removed redundant `stateTransitionExecute` / `innerExecute` forwards (now canonical in kernel headers).
- Updated index comments to reflect Tier E → canonical direction.

## Internal call sites updated

| Area | Change |
| --- | --- |
| `FiscoExecute.cpp` | `stateTransitionExecute` |
| `EthReferenceExecute.cpp` | `stateTransitionExecute` |
| `OpStackTxLifecycle.cpp` | `stateTransitionExecute` (×2) |
| `ChainPrecheckPolicy.h` default | `innerExecute` |
| `OpStackPrecheckPolicy.cpp` | `innerExecute` |
| `TxExecutionRunner.cpp` logs | `innerExecute` |
| bcos-evm tests (~35 files) | canonical names |

## Transaction executor

Audited `transaction-executor/` — **no direct** `runTxPipeline` / `executeMessage` calls. TE enters via Tier E adapters (`fiscoExecute`, `ethReferenceExecute`, `opStackExecute`). No TE code changes required for Phase 3b (documented in ADR-031 §3).

## ADR

Added `bcos-evm/docs/adr/031-te-geth-kernel-symbol-migration.md`.

## Test Results

```text
ctest -R 'GethNaming|TxPipeline|TxExecutionRunner|EthOrchestrationProfile'
4/4 passed (build/)
```

| Test | Result |
| --- | --- |
| GethNamingAliases | Passed |
| TxPipeline | Passed |
| TxExecutionRunner | Passed |
| EthOrchestrationProfile | Passed |

## Commit

`refactor(bcos-evm): promote stateTransitionExecute and innerExecute to canonical kernel names`

## Concerns

None blocking. Deprecated aliases must remain until TE Phase 4–6 migration per ADR-031.
