# Task 2 Report: ADR-032 Wave 2 — Kernel Tier E Forward Removal

**Date:** 2026-06-30  
**Baseline:** `f4f7bc42c` (Wave 1 complete)  
**Status:** DONE

## Summary

Removed deprecated kernel Tier E inline forwards `runTxPipeline` and `executeMessage`. Canonical symbols `stateTransitionExecute` and `innerExecute` are now the only exported kernel entry points in their respective headers.

## rg Gate Audit

```bash
rg '\brunTxPipeline\b|\bexecuteMessage\b' bcos-evm --glob '!docs/**' --glob '!*.md'
rg '\brunTxPipeline\b|\bexecuteMessage\b' transaction-executor
```

| Scope | Result |
| --- | --- |
| `bcos-evm` (non-doc code) | 1 hit — retirement comment in `GethNamingAliases.h` only |
| `transaction-executor` | 0 hits |
| `OpStackExecuteMessageTestHook::executeMessageSpySlot` | Not present / unrelated — untouched |

Production and test call sites already used canonical names before this wave.

## Changes

| File | Change |
| --- | --- |
| `eth/pipeline/TxPipeline.h` | Removed `[[deprecated]] inline runTxPipeline` forward |
| `eth/ExecuteMessage.h` | Removed `[[deprecated]] inline executeMessage` forward |
| `eth/GethNamingAliases.h` | Updated Tier E index; noted Wave 2 removals |
| `test/eth/GethNamingAliasesTest.cpp` | Removed deprecated alias cases; retained canonical driver tests |
| `docs/adr/032-tier-e-symbol-retirement.md` | Wave 2 timeline row; inventory strikethrough; checklist ✅ |

### Collateral fix

`GethNamingAliases.h` `evmStaticCall` assert used non-existent `EVMC_STATICCALL`; corrected to `EVMC_CALL && (message.flags & EVMC_STATIC)` so `GethNamingAliasesTest` compiles.

## Test Summary

Built and ran in `build-bcos-evm-check`:

| Test | Cases | Result |
| --- | --- | --- |
| `GethNamingAliasesTest` | 6 | PASS |
| `TxPipelineTest` | 8 | PASS |
| `TxExecutionRunnerTest` | 9 | PASS |
| `ExecuteMessageSmokeTest` | 2 | PASS |

## Concerns

1. **Documentation drift:** Many architecture docs still reference `runTxPipeline` / `executeMessage` as live symbols (Wave 5 cleanup per ADR-032).
2. **Remaining Tier E:** `fiscoExecute`, `ethReferenceExecute`, `opStackExecute` forwards remain until Wave 3–4.
3. **`evmStaticCall` assert:** Pre-existing compile bug fixed collaterally; not part of Wave 2 scope but required for smoke test build.

## Commit

**Hash:** `ccd5b1ff81979240cba4788f31f055e1f00d755a`  
**Message:** `refactor(evm): ADR-032 Wave 2 — remove kernel Tier E forwards`
