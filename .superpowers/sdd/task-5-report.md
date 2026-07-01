# Task 5 Report — ADR-032 Wave 5 Documentation Cleanup

**Date:** 2026-06-30  
**Baseline:** `3f77f4fd8`  
**Commit message:** `docs(evm): ADR-032 Wave 5 — retirement doc cleanup`

## Summary

Wave 5 completes the Tier E symbol retirement documentation sweep after Waves 1–4 removed all deprecated forwards from code. No production symbol changes in this wave — docs, log strings, and ADR checklists only.

## Changes

### Carry-over from Wave 3/4 reviews

| Item | Status | Notes |
| --- | --- | --- |
| `Initializer.cpp` log strings → `apply*Message` | ✅ | Comments + 3 `INITIALIZER_LOG` lines updated |
| ADR-032 §3 gate checklist | ✅ | Waves 1–4 gates marked complete |
| ADR-032 §4 TE checklist | ✅ | Wave 3–4 items marked complete (optional `*ExecuteTx()` rename left open) |
| ADR-032 internal migration checklist | ✅ | Wave 1 items marked complete |
| `architecture-overview.md` | ✅ | Canonical names throughout; Tier E dual-label removed |
| `opstack/README.md` | ✅ | `applyOpStackMessage` canonical |
| `bcos/README.md` | ✅ | `applyFiscoMessage` + `stateTransitionExecute` |
| `eth/README.md` | ✅ | Tier E removal noted; canonical flow updated |
| ADR-030 §8 stable-alias table | ✅ | All removed symbols marked with 2026-06-30 dates |
| ADR-031 appendix timeline | ✅ | Waves 1–5 events added |
| `GethNamingAliases.h` index | ✅ | Stale Tier E entries replaced with Wave 5 sweep note |
| `PrecompileRouterInput` removal | ✅ N/A | Already absent (grep clean) |

### ADR updates

- **ADR-032:** Wave 5 timeline row, CI confirmation appendix, Tier E inventory fully struck through
- **ADR-031:** TE §3 updated to `apply*Message`; deprecated forward note reflects Wave 2 removal
- **ADR-030:** §2 entry points, §3 step map, §8 removal table, Tier A alias table, Appendix A lookup

## Verification

```bash
cd build && ctest -R 'GethNaming|FiscoExecute|EthReference|OpStackExecute|TxPipeline' --output-on-failure
```

**Result:** 9/9 passed (0.43s)

| Test | Result |
| --- | --- |
| EthReferenceExecuteFixture | Passed |
| EthReferenceExecute1559GasPrice | Passed |
| TxPipeline | Passed |
| GethNamingAliases | Passed |
| FiscoExecuteSmoke | Passed |
| Bcos7702FiscoExecutePropagation | Passed |
| FiscoExecuteImportedFixture | Passed |
| Bcos7212FiscoExecute | Passed |
| OpStackExecuteSmoke | Passed |

## Out of scope (documented, not blocking)

- TE local helper names (`fiscoExecuteTx()`, etc.) — optional hygiene per ADR-032 §4
- `.superpowers/` not committed per instructions

## Files touched

- `libinitializer/Initializer.cpp`
- `bcos-evm/eth/GethNamingAliases.h`
- `bcos-evm/docs/adr/030-geth-naming-map.md`
- `bcos-evm/docs/adr/031-te-geth-kernel-symbol-migration.md`
- `bcos-evm/docs/adr/032-tier-e-symbol-retirement.md`
- `bcos-evm/docs/architecture-overview.md`
- `bcos-evm/bcos/README.md`
- `bcos-evm/eth/README.md`
- `bcos-evm/opstack/README.md`
