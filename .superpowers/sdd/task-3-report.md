# Task 3 Report: Phase 4a — apply*Message as primary documented names

**Status:** DONE  
**Branch:** feat/adr-030-geth-naming  
**Baseline:** 0c78e63b02fcfdd6ce6c3f8e2797ef10f79f17ce  
**Date:** 2026-06-30

---

## Summary

Documented ADR-030 Tier C `apply*Message` as the primary geth `ApplyMessage` vocabulary in architecture docs and `eth/README.md`. Verified chain-header inline aliases from Task 1; no `[[deprecated]]` added on Tier E `*Execute` symbols.

---

## Deliverables

| Item | Result |
| --- | --- |
| `applyReferenceMessage` in `EthReferenceExecute.h` | Present (Task 1) |
| `applyFiscoMessage` in `FiscoExecute.h` | Present (Task 1) |
| `applyOpStackMessage` in `OpStackExecute.h` | Present (Task 1) |
| No `[[deprecated]]` on `*Execute` | Confirmed |
| `bcos-evm/eth/README.md` — fix `EthPipelineHookBinder` → `EthOrchestrationProfile` | Done |
| `bcos-evm/eth/README.md` — dual-label table + execution flow | Done |
| `bcos-evm/docs/architecture-overview.md` — §2 mermaid/ASCII + §3/§3.4 dual-label flows | Done |
| `GethNamingAliases.h` — Tier A/C/E index with chain header locations | Done |

---

## Files changed

| File | Change |
| --- | --- |
| `bcos-evm/eth/README.md` | Dual-label chain entry table; updated execution flow; removed stale `EthPipelineHookBinder` |
| `bcos-evm/docs/architecture-overview.md` | ADR-029+030 dual-label convention; entry table with geth column; §3.4 flow diagram |
| `bcos-evm/eth/GethNamingAliases.h` | Expanded index: Tier A/C/E split; chain header paths for `apply*Message` |

---

## Verification

- Docs-only change; no TE ABI breakage.
- Tier E symbols unchanged: `ethReferenceExecute`, `fiscoExecute`, `opStackExecute`.
- No new tests required (documentation task).

---

## Follow-up (out of scope)

- Phase 4b+: add `[[deprecated("use apply*Message")]]` on Tier E `*Execute` when TE migration plan lands.
- Remaining `architecture-overview.md` kernel sections still cite `TxExecutionRunner::run` / `runExecutionFrame` in §3.1–3.2 prose (ADR-029 rename from Task 2); update in a dedicated doc sweep.
