# Task 4 Report — ADR-032 Wave 4

**Date:** 2026-06-30  
**Baseline:** `5476b125c`  
**Commit:** `3f77f4fd8` — `refactor(evm): ADR-032 Wave 4 — remove *Execute Tier E symbols`

## Summary

Removed deprecated Tier E inline forwards from chain adapter headers. Canonical entry points are now `applyFiscoMessage`, `applyEthMessage`, and `applyOpStackMessage` only.

## Changes

| File | Change |
| --- | --- |
| `bcos-evm/bcos/FiscoExecute.h` | Removed `fiscoExecute` deprecated inline forward |
| `bcos-evm/eth/apply/EthReferenceExecute.h` | Removed `ethReferenceExecute` deprecated inline forward |
| `bcos-evm/opstack/OpStackExecute.h` | Removed `opStackExecute` deprecated inline forward |
| `bcos-evm/eth/GethNamingAliases.h` | Replaced Tier E index with Wave 4 removal note |
| `bcos-evm/docs/adr/032-tier-e-symbol-retirement.md` | Timeline + inventory: Wave 4 marked done (2026-06-30) |

### Aggregate headers (unchanged — already canonical-only)

- `bcos-evm/include/bcos-evm/fisco_executor.hpp` → includes `FiscoExecute.h`
- `bcos-evm/include/bcos-evm/eth_executor.hpp` → includes `EthReferenceExecute.h`
- `bcos-evm/include/bcos-evm/op_executor.hpp` → includes `OpStackExecute.h`

## Gate (monorepo rg)

```bash
rg '\bfiscoExecute\b|\bethReferenceExecute\b|\bopStackExecute\b' --glob '*.{cpp,h,hpp}'
```

**Result:** PASS — zero callable symbols. Remaining hits are allowed:

- Comments / `@brief` in test files (test case documentation)
- Log strings in `libinitializer/Initializer.cpp`
- ADR/history references in `GethNamingAliases.h` removal note
- TE compat test description strings

All production and test **call sites** already use `apply*Message` (TE adapters, bcos-evm tests).

## Tests

```bash
ctest -R 'FiscoExecuteSmoke|EthReferenceExecuteFixture|OpStackExecuteSmoke|CompatExecuteViaHost' --output-on-failure -j8
```

**Result:** 109/109 passed (0 failed)

Includes:

- `FiscoExecuteSmoke`, `FiscoExecuteImportedFixture`, `Bcos7702FiscoExecutePropagation`, `Bcos7212FiscoExecute`
- `EthReferenceExecuteFixture`, `EthReferenceExecute1559GasPrice`
- `OpStackExecuteSmoke`
- `CompatExecuteViaHost/*` (TE forward-compat)

## Wave 5 follow-up (not in scope)

- Stale Tier E rows in ADR-030 §8, architecture-overview.md, chain READMEs
- Initializer log strings still mention legacy `*Execute` names
