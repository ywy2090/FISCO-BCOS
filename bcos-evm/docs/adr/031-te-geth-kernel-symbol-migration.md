# ADR-031: Transaction Executor — geth Kernel Symbol Migration

**Status:** Accepted  
**Date:** 2026-06-30  
**Related:** ADR-030, ADR-029, ADR-032, ADR-019, `GethNamingAliases.h`, `transaction-executor/`  
**Phase:** P1 / Phase 3b (geth naming P0–P6 plan)

---

## Context

ADR-030 introduced geth vocabulary as **Tier A inline aliases** forwarding to legacy FISCO names (`runTxPipeline`, `executeMessage`). That let reviewers use geth terms in comments and new code without breaking the **Tier E stable ABI** consumed by `transaction-executor` (TE).

Phase 3b promotes two **portable eth kernel** entry points to canonical C++ identifiers inside `bcos-evm/eth/`:

| Legacy (Tier E) | geth analogue | ADR-031 canonical |
| --- | --- | --- |
| `runTxPipeline` | `stateTransition.execute` | `stateTransitionExecute` |
| `executeMessage` | `innerExecute` (post-`Prepare` EVM) | `innerExecute` |

Chain ApplyMessage adapters (`fiscoExecute`, `ethReferenceExecute`, `opStackExecute`) remain Tier E until a separate TE-facing ADR schedules their rename to `apply*Message`.

---

## Decision

### 1. Canonical kernel symbols (Phase 3b — **done**)

| Symbol | Header | Implementation | Deprecated alias |
| --- | --- | --- | --- |
| `stateTransitionExecute` | `eth/pipeline/TxPipeline.h` | `TxPipeline.cpp` | ~~`[[deprecated]] inline runTxPipeline`~~ removed Wave 2 (2026-06-30) |
| `innerExecute` | `eth/ExecuteMessage.h` | `ExecuteMessage.cpp` | ~~`[[deprecated]] inline executeMessage`~~ removed Wave 2 (2026-06-30) |

**Rules:**

- New `bcos-evm` code **must** call canonical names.
- Tier E aliases remain **one release minimum** for TE and external callers; removal requires explicit TE migration + ADR update.
- `GethNamingAliases.h` drops redundant forwards for promoted symbols; keep Tier A aliases that still forward (`prepareState`, `evmCall`, …).

### 2. Internal migration (bcos-evm)

| Area | Change |
| --- | --- |
| Chain bridges | `FiscoExecute`, `EthReferenceExecute`, `OpStackTxLifecycle` call `stateTransitionExecute` |
| `ChainPrecheckPolicy::pipelineInvokeEvmKernel` default | calls `innerExecute` |
| `OpStackPrecheckPolicy` override | calls `innerExecute` |
| Tests | Prefer canonical names; retain deprecated-alias coverage in `GethNamingAliasesTest` |

Log strings in `TxPipeline.cpp` / `TxExecutionRunner.cpp` use canonical names where they name the kernel step.

### 3. Transaction executor (TE)

TE does **not** call `runTxPipeline` / `executeMessage` directly; it enters via chain L1 adapters (ADR-032 Waves 3–4 complete):

```text
TransactionExecutorImpl  → applyFiscoMessage
EthTransactionExecutorImpl → applyReferenceMessage
OpStackTransactionExecutorImpl → applyOpStackMessage
```

**Phase 3b TE action:** none required for direct kernel symbols. TE uses `apply*Message` at the execute boundary since P2; Tier E `*Execute` forwards removed Wave 4 (2026-06-30).

**Future TE schedule:** see **ADR-032** (Tier E retirement waves 1–5). Summary:

| Phase | TE change | bcos-evm alias removal |
| --- | --- | --- |
| P2 ✅ | TE calls `apply*Message` at execute boundary | retain `*Execute` exported symbols |
| Wave 3 | `apply*Message` becomes exported link symbol; `*Execute` deprecated | ADR-032 §1 Wave 3 |
| Wave 4 | TE on canonical names only | remove `fiscoExecute`, `ethReferenceExecute`, `opStackExecute` |
| Wave 2 | (no TE action) | remove `runTxPipeline`, `executeMessage` |

### 4. ADR-029 coexistence

ADR-029 layer prefixes (`pipeline*`, `runEvmKernelTopLevel`, `runCallFrame`) are unchanged. ADR-031 only renames the **L2 driver** and **L3 stable facade**:

```text
*Execute (L1) → stateTransitionExecute (L2 driver) → innerExecute (L3 facade) → runEvmKernelTopLevel (L3 body)
```

Both ADR-029 and geth names appear in comments during transition.

---

## Consequences

- Parity reviews cite `stateTransitionExecute` / `innerExecute` as primary kernel symbols; `runTxPipeline` / `executeMessage` are legacy ABI.
- CI must keep `GethNamingAliasesTest` verifying deprecated forwards until alias removal.
- Documentation (`architecture-overview.md`, ADR-030 §8) should note Phase 3b completion; Tier E table lists deprecated forwards explicitly.

---

## Compliance checklist (Phase 3b)

- [x] `stateTransitionExecute` canonical in `TxPipeline.h/.cpp`
- [x] `innerExecute` canonical in `ExecuteMessage.h/.cpp`
- [x] `[[deprecated]]` inline `runTxPipeline` / `executeMessage` retained *(removed Wave 2, 2026-06-30)*
- [x] `GethNamingAliases.h` — removed duplicate forwards for promoted symbols
- [x] bcos-evm internal call sites updated
- [x] TE audited — no direct kernel calls; no TE code change required
- [x] `GethNamingAliasesTest` + pipeline/orchestration tests green

---

## Appendix — Symbol timeline

| Date | Event |
| --- | --- |
| 2026-06-29 | ADR-030: geth aliases forward **to** legacy names |
| 2026-06-30 | ADR-031 Phase 3b: legacy aliases forward **to** geth canonical names |
| 2026-06-30 | ADR-032 Wave 1: internal transitional aliases removed |
| 2026-06-30 | ADR-032 Wave 2: `runTxPipeline` / `executeMessage` kernel Tier E forwards removed |
| 2026-06-30 | ADR-032 Wave 3: `apply*Message` promoted to exported link symbols |
| 2026-06-30 | ADR-032 Wave 4: `fiscoExecute` / `ethReferenceExecute` / `opStackExecute` removed |
| 2026-06-30 | ADR-032 Wave 5: documentation + aggregate header cleanup (this wave) |
