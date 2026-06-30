# Task 4 Report: OpStack lifecycleCheckEntryRules (P3)

**Status:** DONE  
**Branch:** feat/adr-030-geth-naming  
**Baseline:** c93e0caeaa  
**Date:** 2026-06-30

---

## Summary

Renamed `OpStackPrecheckPolicy::checkEntryRules` → `lifecycleCheckEntryRules` per ADR-029 §5 (OpStack L1½ lifecycle prefix). Added deprecated inline `checkEntryRules` alias forwarding to the canonical name. Updated call sites, test helper, and `opstack/README.md`.

---

## Deliverables

| Item | Result |
| --- | --- |
| Canonical method `lifecycleCheckEntryRules` | Done (`OpStackPrecheckPolicy.h/.cpp`) |
| Deprecated `checkEntryRules` alias | Done (inline, ADR-029 message) |
| `OpStackTxLifecycle.cpp` call site | Updated |
| Test helper `OpStackEntryPrecheck.h` | Updated |
| `opstack/README.md` | Updated (also `pipelineCheckGasAffordable` in module table) |

---

## Files changed

| File | Change |
| --- | --- |
| `bcos-evm/opstack/OpStackPrecheckPolicy.h` | Rename + deprecated alias |
| `bcos-evm/opstack/OpStackPrecheckPolicy.cpp` | Implementation rename |
| `bcos-evm/opstack/OpStackTxLifecycle.cpp` | Call site |
| `bcos-evm/test/helpers/OpStackEntryPrecheck.h` | Test helper call site |
| `bcos-evm/opstack/README.md` | Module table + execution flow diagram |

---

## Verification

```bash
cd build
cmake --build . --target OpStackPrecheckPolicyTest OpStackOrchestrationProfileTest \
  OpStackOrchestrationErrorPolicyTest OpStackTxLifecycleCharacterizationTest
ctest -R 'OpStackOrchestration|OpStackTxLifecycle|OpStackPrecheck' --output-on-failure
```

| Test | Result |
| --- | --- |
| OpStackOrchestrationProfile | PASS |
| OpStackPrecheckPolicy | PASS |
| OpStackOrchestrationErrorPolicy | PASS |
| OpStackTxLifecycleCharacterization | PASS |

**4/4 matched tests pass.**

---

## Notes

- ADR/historical docs still mention `checkEntryRules` in prose; code uses `lifecycleCheckEntryRules`.
- Deprecated alias retained for one release cycle (same pattern as `ChainPrecheckPolicy` ADR-029 aliases).
