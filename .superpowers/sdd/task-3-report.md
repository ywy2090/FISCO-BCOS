# Task 3 Report — ADR-032 Wave 3 Chain Adapter Promotion

**Date:** 2026-06-30  
**Baseline:** `ccd5b1ff8`  
**Commit message:** `refactor(evm): ADR-032 Wave 3 — promote apply*Message exports`

## Summary

Flipped chain adapter entry symbols so `apply*Message` is the exported link symbol and `*Execute` are `[[deprecated]]` inline forwards (Wave 4 removal).

## Changes

### Chain adapters (exported ↔ deprecated flip)

| Adapter | Header | .cpp implementation | Deprecated forward |
| --- | --- | --- | --- |
| FISCO | `applyFiscoMessage` | `FiscoExecute.cpp` | `fiscoExecute` |
| ETH reference | `applyEthMessage` | `EthReferenceExecute.cpp` | `ethReferenceExecute` |
| OP Stack | `applyOpStackMessage` | `OpStackExecute.cpp` | `opStackExecute` |

- `@brief` tags and `LOG_DESC` strings updated to canonical `apply*` names.
- `.cpp` filenames unchanged (Wave 5 may rename).

### Documentation / aliases

- `GethNamingAliases.h` — Wave 3 status; Tier C/Tier E direction corrected.
- `bcos-evm/docs/adr/032-tier-e-symbol-retirement.md` — Wave 3 row (2026-06-30); appendix deprecated-since dates.

### Tests

- Migrated **34** bcos-evm test `.cpp` call sites from `*Execute(` → `apply*Message(`.
- Test case names retaining `*Execute` in identifiers left unchanged (BOOST_AUTO_TEST_CASE labels).

### TE hygiene (comments only; call sites unchanged)

- `EthTransactionExecutorImpl.h`, `OpStackTransactionExecutorImpl.h`
- `ExecuteViaHostCompatTest.cpp`, `ExecuteViaHostEip2929Harness.h`

## Gate verification

| Gate | Status |
| --- | --- |
| TE invokes `applyFiscoMessage` / `applyEthMessage` / `applyOpStackMessage` only | ✅ Confirmed (`TransactionExecutorImpl.h:316`, `EthTransactionExecutorImpl.h:251`, `OpStackTransactionExecutorImpl.h:228`) |
| `*Execute` forwards retained (Wave 4) | ✅ |
| No `.superpowers/` in commit | ✅ |

## Tests run

| Test binary | Result |
| --- | --- |
| `FiscoExecuteSmokeTest` | PASS |
| `FiscoExecuteImportedFixtureTest` | PASS |
| `Bcos7702FiscoExecutePropagationTest` | PASS |
| `EthReferenceExecuteFixtureTest` | PASS |
| `OpStackExecuteSmokeTest` | PASS |
| `ExecuteViaHostCompat` | **Not in current build graph** (target absent from `build/` Makefile) |

Log strings confirm canonical names, e.g. `applyFiscoMessage done`, `applyEthMessage done`.

## Follow-ups (Wave 4+)

- Remove `[[deprecated]]` `*Execute` inline forwards.
- Wave 5: optional `.cpp` filename rename + doc sweep.
