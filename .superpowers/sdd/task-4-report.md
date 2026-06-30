# Task 4 Report: Phase 4b — Rename `eth/reference/` → `eth/apply/`

**Status:** DONE  
**Branch:** feat-evm-refactor  
**Baseline:** e66bb1141  
**Commit:** (see below)  
**Date:** 2026-06-30

---

## Summary

Moved ETH reference orchestration sources from `bcos-evm/eth/reference/` to `bcos-evm/eth/apply/`. Filenames kept as `EthReferenceExecute.*` etc. to avoid transaction-executor churn. All `#include` paths, docs, CI script, and transaction-executor headers updated.

---

## Deliverables

| Item | Result |
| --- | --- |
| Directory move `reference/` → `apply/` | Done (git mv, 11 files) |
| Filenames unchanged (`EthReferenceExecute.*`, etc.) | Confirmed |
| `#include` paths in bcos-evm | Updated (apply/, eth/, test/, include/) |
| transaction-executor includes | Updated (`EthTransactionExecutorImpl.h`, `EthTxInputBuilder.h`) |
| CMakeLists | No change needed — `GLOB_RECURSE eth/*.cpp` picks up new path |
| Docs (`bcos-evm/docs/`, `eth/README.md`) | Updated |
| CI script (`check-opstack-no-prague-post-execution.sh`) | Updated |

---

## Files changed (high level)

| Area | Files |
| --- | --- |
| Moved sources | 11 files under `bcos-evm/eth/apply/` |
| Includes | 18 test/fixture/TE files + `eth/pipeline/FeeInputsMapping.h`, `include/bcos-evm/eth_executor.hpp` |
| Docs | 4 ADR/architecture docs + `eth/README.md` |
| CI | `tools/ci/check-opstack-no-prague-post-execution.sh` |

---

## Verification

```bash
cd build && cmake --build . --target bcos-evm-eth EthReferenceExecuteFixtureTest \
  EthOrchestrationProfileTest TxPipelineTest GethNamingAliasesTest \
  EthReferenceExecute1559GasPriceTest -j$(sysctl -n hw.ncpu)

ctest -R 'EthReference|GethNaming|OrchestrationProfile|TxPipeline' --output-on-failure
```

| Test | Result |
| --- | --- |
| EthReferenceExecuteFixture | PASS |
| EthReferenceExecute1559GasPrice | PASS |
| TxPipeline | PASS |
| GethNamingAliases | PASS |
| EthOrchestrationProfile | **FAIL** (pre-existing: `pre_execute_precheck_early_exit`) |
| FiscoOrchestrationProfile | PASS |
| OpStackOrchestrationProfile | PASS |

**6/7 matched tests pass.** `EthOrchestrationProfile` failure is unrelated to the directory rename — precheck early-exit assertion (`gasTipCap > gasFeeCap`) fails on current branch baseline; not introduced by this change.

Build of `bcos-evm-eth` and all EthReference* targets: **PASS**.

---

## Notes

- Symbol names (`ethReferenceExecute`, `EthOrchestrationProfile`, etc.) unchanged.
- Tier C `applyReferenceMessage` inline alias remains in `EthReferenceExecute.h`.
- Historical docs under `docs/superpowers/` not updated (out of bcos-evm scope).
