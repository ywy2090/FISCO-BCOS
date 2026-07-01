# Task 6 (P5) Report — Tier E retirement ADR

**Plan:** `docs/superpowers/plans/2026-06-30-geth-naming-p0-p6.md`  
**Brief:** `.superpowers/sdd/task-6-brief.md`  
**Date:** 2026-06-30

## Deliverables

| Item | Path | Notes |
| --- | --- | --- |
| New ADR | `bcos-evm/docs/adr/032-tier-e-symbol-retirement.md` | Authoritative Wave 1–5 removal order, gates, TE + bcos-evm checklists |
| ADR-031 cross-ref | `bcos-evm/docs/adr/031-te-geth-kernel-symbol-migration.md` | §3 future schedule + appendix timeline point to ADR-032 |

## Scope

- **Docs only** — no code or symbol removal.
- Chose **new ADR-032** over extending ADR-031 alone so retirement waves stay separate from Phase 3b kernel promotion.

## ADR-032 summary

1. **Wave 1** — Internal deprecated aliases (`debitIntrinsicGas`, `runExecutionFrame`, ChainPrecheckPolicy legacy virtuals, `checkEntryRules`, …).
2. **Wave 2** — Kernel Tier E (`runTxPipeline`, `executeMessage`).
3. **Wave 3** — Promote `apply*Message` to exported symbols; deprecate `*Execute`.
4. **Wave 4** — Remove `fiscoExecute`, `ethReferenceExecute`, `opStackExecute`.
5. **Wave 5** — Doc/test/aggregate-header cleanup.

TE checklist records P2 complete (`apply*Message` at execute boundary); Waves 3–4 are TE-blocking.

## Verification

- No production code changed.
- ADR content aligned with current headers (`TxPipeline.h`, `ExecuteMessage.h`, chain `*Execute.h`, `GethNamingAliasesTest.cpp`, TE impl grep).

## Commit

```
docs(bcos-evm): add ADR for Tier E symbol retirement schedule
```
