# ADR-031: Transaction Executor — geth Kernel Symbol Migration

**Status:** Accepted  
**Date:** 2026-06-30  
**Related:** ADR-030, ADR-029, ADR-019, `GethNamingAliases.h`, `transaction-executor/`  
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
| `stateTransitionExecute` | `eth/pipeline/TxPipeline.h` | `TxPipeline.cpp` | `[[deprecated]] inline runTxPipeline` |
| `innerExecute` | `eth/ExecuteMessage.h` | `ExecuteMessage.cpp` | `[[deprecated]] inline executeMessage` |

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

TE does **not** call `runTxPipeline` / `executeMessage` directly today; it enters via Tier E chain adapters:

```text
TransactionExecutorImpl  → fiscoExecute
EthTransactionExecutorImpl → ethReferenceExecute
OpStackTransactionExecutorImpl → opStackExecute
```

**Phase 3b TE action:** none required for direct kernel symbols. TE continues using `*Execute` until Phase 4+ documents `apply*Message` promotion.

**Future TE schedule (proposed — not in 3b scope):**

| Phase | TE change | bcos-evm alias removal |
| --- | --- | --- |
| 4 | TE adopts `applyFiscoMessage` / `applyReferenceMessage` names in new code; keep `*Execute` deprecated forwards | retain `fiscoExecute`, `ethReferenceExecute` |
| 5 | OP TE adopts `applyOpStackMessage` | retain `opStackExecute` |
| 6 | TE drops all Tier E kernel + adapter aliases | remove `runTxPipeline`, `executeMessage`, `*Execute` |

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
- [x] `[[deprecated]]` inline `runTxPipeline` / `executeMessage` retained
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
| TBD Phase 6 | Remove Tier E aliases after TE migration |
