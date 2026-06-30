# ADR-031: Transaction Executor — geth Kernel Symbol Migration

**Status:** Accepted  
**Date:** 2026-06-30  
**Related:** ADR-030, ADR-029, ADR-032, ADR-019, `transaction-executor/`  
**Phase:** P1 / Phase 3b (geth naming P0–P6 plan)

---

## Context

ADR-030 introduced geth vocabulary as **Tier A inline aliases** (`prepareState`, `evmCall`, …) alongside ADR-029 layer prefixes. During Phase 3b (2026-06-30), two **portable eth kernel** entry points were promoted to canonical C++ identifiers; Tier E forwards were removed in ADR-032 Waves 2–4.

| ~~Legacy (Tier E)~~ | geth analogue | ADR-031 canonical |
| --- | --- | --- |
| ~~`runTxPipeline`~~ | `stateTransition.execute` | `stateTransitionExecute` |
| ~~`executeMessage`~~ | `innerExecute` (post-`Prepare` EVM) | `innerExecute` |

Chain ApplyMessage adapters are **`applyFiscoMessage` / `applyReferenceMessage` / `applyOpStackMessage`** (exported since ADR-032 Wave 3; `*Execute` removed Wave 4).

---

## Decision

### 1. Canonical kernel symbols (Phase 3b — **done**)

| Symbol | Header | Implementation | Deprecated alias |
| --- | --- | --- | --- |
| `stateTransitionExecute` | `eth/pipeline/StateTransitionExecute.h` | `TxPipeline.cpp` | ~~`[[deprecated]] inline runTxPipeline`~~ removed Wave 2 (2026-06-30) |
| `innerExecute` | `eth/InnerExecute.h` | `ExecuteMessage.cpp` | ~~`[[deprecated]] inline executeMessage`~~ removed Wave 2 (2026-06-30) |

**Rules:**

- New `bcos-evm` code **must** call canonical names (`stateTransitionExecute`, `innerExecute`, `apply*Message`).
- ~~Tier E aliases remain one release minimum~~ — **removed ADR-032 Waves 2–4 (2026-06-30)**.
- ~~`GethNamingAliases.h`~~ removed 2026-06-30; use canonical symbols (`warmTransactionEntry`, `runCallFrame`, …) per ADR-030 §3.

### 2. Internal migration (bcos-evm)

| Area | Change |
| --- | --- |
| Chain bridges | `FiscoExecute`, `EthReferenceExecute`, `OpStackTxLifecycle` call `stateTransitionExecute` |
| `ChainPrecheckPolicy::pipelineInvokeEvmKernel` default | calls `innerExecute` |
| `OpStackPrecheckPolicy` override | calls `innerExecute` |
| Tests | Prefer canonical names; deprecated-alias tests removed with symbols (ADR-032) |

Log strings in `TxPipeline.cpp` / `TxExecutionRunner.cpp` use canonical names where they name the kernel step.

### 3. Transaction executor (TE)

TE does **not** call `runTxPipeline` / `executeMessage` directly; it enters via chain L1 adapters (ADR-032 Waves 3–4 complete):

```text
TransactionExecutorImpl  → applyFiscoMessage
EthTransactionExecutorImpl → applyReferenceMessage
OpStackTransactionExecutorImpl → applyOpStackMessage
```

**Phase 3b TE action:** none required for direct kernel symbols. TE uses `apply*Message` at the execute boundary since P2; Tier E `*Execute` forwards removed Wave 4 (2026-06-30).

**TE schedule (complete):** see **ADR-032** Waves 1–5. Summary:

| Phase | TE change | bcos-evm |
| --- | --- | --- |
| P2 ✅ | TE calls `apply*Message` at execute boundary | `*Execute` still exported |
| Wave 3 ✅ | `apply*Message` exported link symbol | `*Execute` deprecated inline |
| Wave 4 ✅ | TE on canonical names only | `*Execute` forwards removed |
| Wave 2 ✅ | (no TE action) | `runTxPipeline`, `executeMessage` removed |

### 4. ADR-029 coexistence

ADR-029 layer prefixes (`pipeline*`, `runEvmKernelTopLevel`, `runCallFrame`) are unchanged. ADR-031 only renames the **L2 driver** and **L3 stable facade**:

```text
apply*Message (L1) → stateTransitionExecute (L2 driver) → innerExecute (L3 facade) → runEvmKernelTopLevel (L3 body)
```

Both ADR-029 and geth names appear in comments during transition.

---

## Consequences

- Parity reviews cite `stateTransitionExecute` / `innerExecute` / `apply*Message` as primary symbols; Tier E names are historical only (ADR-032).
- `KernelCanonicalNamingTest` verifies canonical kernel drivers and policy-level geth forwards.
- Documentation (`architecture-overview.md`, ADR-030 §8) reflects post-retirement canonical names.

---

## Compliance checklist (Phase 3b)

- [x] `stateTransitionExecute` canonical in `TxPipeline.h/.cpp`
- [x] `innerExecute` canonical in `ExecuteMessage.h/.cpp`
- [x] `[[deprecated]]` inline `runTxPipeline` / `executeMessage` retained *(removed Wave 2, 2026-06-30)*
- [x] `GethNamingAliases.h` removed 2026-06-30; canonical symbols only
- [x] bcos-evm internal call sites updated
- [x] TE audited — no direct kernel calls; no TE code change required
- [x] `KernelCanonicalNamingTest` + pipeline/orchestration tests green

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
