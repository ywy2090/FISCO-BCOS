# ADR-032: Tier E Symbol Retirement Schedule

**Status:** Accepted (plan only — no symbol removal in this ADR)  
**Date:** 2026-06-30  
**Related:** ADR-030, ADR-031, ADR-029, `GethNamingAliases.h`, `transaction-executor/`  
**Phase:** P5 (geth naming P0–P6 plan)

---

## Context

ADR-030 defined **Tier E — stable ABI**: legacy FISCO entry names (`executeMessage`, `fiscoExecute`, `runTxPipeline`, …) that external consumers—primarily `transaction-executor` (TE)—link against while `bcos-evm/eth/` adopts geth vocabulary.

ADR-031 Phase 3b (P1) promoted two **portable eth kernel** symbols to canonical C++ identifiers and added `[[deprecated]]` inline forwards:

| Tier E (deprecated forward) | Canonical (ADR-031) | Header |
| --- | --- | --- |
| `runTxPipeline` | `stateTransitionExecute` | `eth/pipeline/TxPipeline.h` |
| `executeMessage` | `innerExecute` | `eth/ExecuteMessage.h` |

Phase 4c (P2) documented **`apply*Message`** as the geth-aligned chain entry name; TE impls now call `applyFiscoMessage` / `applyReferenceMessage` / `applyOpStackMessage` at the syscall boundary while `*Execute` remains the exported function symbol (no `[[deprecated]]` yet).

This ADR is the **authoritative removal schedule** for all Tier E and ADR-029 transitional aliases. **No symbol is removed until its wave gate checklist is complete.**

**Non-goals (this ADR):**

- Deleting deprecated forwards or renaming `*Execute` implementations.
- Changing ADR-029 layer prefixes (`pipeline*`, `runCallFrame`) — those remain valid parallel vocabulary until a separate rename ADR.

---

## Decision

### 1. Retirement waves (strict order)

Remove symbols **only** in wave order. Each wave requires the gate in §3 before merge.

```text
Wave 1 — bcos-evm internal transitional aliases (no TE direct calls)
Wave 2 — eth kernel Tier E forwards (runTxPipeline, executeMessage)
Wave 3 — chain adapter promotion (*Execute deprecated; apply*Message canonical)
Wave 4 — chain adapter Tier E removal (fiscoExecute, ethReferenceExecute, opStackExecute)
Wave 5 — docs / test / aggregate-header cleanup
```

#### Wave 1 — Internal transitional aliases

These symbols are `[[deprecated]]` today and are **not** TE entry points. Safe to remove once `bcos-evm` + `bcos-evm/test` grep clean and `GethNamingAliasesTest` cases updated.

| Order | Remove | Canonical replacement | Location |
| --- | --- | --- | --- |
| 1.1 | `debitIntrinsicGas` | `deductIntrinsicGas` | `eth/pipeline/IntrinsicGasDebit.h` |
| 1.2 | `runExecutionFrame` | `runCallFrame` | `eth/execution/ExecutionFrame.h` |
| 1.3 | `TxExecutionRunner::run` | `runEvmKernelTopLevel` | `eth/execution/TxExecutionRunner.h` |
| 1.4 | `buildExecuteMessageInput` | `EvmTxContextView::toExecuteMessageInput` via `ctx.txContextView` | `eth/pipeline/EvmTxContextView.h` |
| 1.5 | `ChainPrecheckPolicy` legacy virtuals: `setupMessage`, `checkTransactionRules`, `checkGasAffordable`, `checkBalanceAndValue`, `tuneExecutionInput`, `runEvmExecution` | `pipelineSetupMessage`, `pipelineCheckRules`, `pipelineCheckGasAffordable`, `pipelineCheckBalance`, `pipelineTuneKernelInput`, `pipelineInvokeEvmKernel` | `eth/pipeline/ChainPrecheckPolicy.h` |
| 1.6 | `checkEntryRules` | `lifecycleCheckEntryRules` | `opstack/OpStackPrecheckPolicy.h` |
| 1.7 | `dispatchPrecompile` | `resolveCallTarget` + `executePrecompileEnvelope` | `eth/precompiled/PrecompileRouter.h` |

**Note:** Tier A geth inline aliases in `GethNamingAliases.h` (`prepareState`, `evmCall`, `preCheckRules`, …) are **not** Tier E and are **out of scope** for this retirement schedule unless a future ADR promotes them to canonical renames.

#### Wave 2 — Eth kernel Tier E forwards

TE does **not** call these directly (verified ADR-031 §3); removal is gated on bcos-evm internal + test migration only.

| Order | Remove | Canonical replacement | Location |
| --- | --- | --- | --- |
| 2.1 | `runTxPipeline` | `stateTransitionExecute` | `eth/pipeline/TxPipeline.h` |
| 2.2 | `executeMessage` | `innerExecute` | `eth/ExecuteMessage.h` |

After Wave 2: delete `GethNamingAliasesTest` cases `runTxPipeline_deprecated_alias_*` and `executeMessage_deprecated_alias_*`; retain canonical driver tests.

#### Wave 3 — Chain adapter promotion

Flip which symbol is the **exported** function vs inline forward. TE already invokes `apply*Message` (P2); this wave makes that name the link symbol.

| Order | Action | geth analogue | Headers |
| --- | --- | --- | --- |
| 3.1 | Rename implementation `fiscoExecute` → keep body; export as `applyFiscoMessage`; add `[[deprecated]] inline fiscoExecute` forward | `ApplyMessage` | `bcos/FiscoExecute.h` |
| 3.2 | Same for `ethReferenceExecute` → `applyReferenceMessage` | `ApplyMessage` | `eth/apply/EthReferenceExecute.h` |
| 3.3 | Same for `opStackExecute` → `applyOpStackMessage` | `ApplyMessage` + op lifecycle | `opstack/OpStackExecute.h` |

Update log strings and `@brief` tags to canonical names; retain `*Execute` in release notes for one minimum release.

#### Wave 4 — Chain adapter Tier E removal

| Order | Remove | Canonical replacement | Consumers |
| --- | --- | --- | --- |
| 4.1 | `fiscoExecute` deprecated forward | `applyFiscoMessage` | TE FISCO path (already on apply) |
| 4.2 | `ethReferenceExecute` deprecated forward | `applyReferenceMessage` | TE ETH reference path |
| 4.3 | `opStackExecute` deprecated forward | `applyOpStackMessage` | TE OP path |

Search entire monorepo for remaining `*Execute(` call sites before merge. Aggregate headers (`include/bcos-evm/fisco_executor.hpp`, `eth_executor.hpp`, `op_executor.hpp`) must re-export canonical names only.

#### Wave 5 — Cleanup

- Remove stale Tier E rows from ADR-030 §8 stable-alias table (or mark removed with date).
- Update `architecture-overview.md`, chain READMEs, and ADR-031 appendix timeline.
- Drop `GethNamingAliases.h` Tier E index comments for removed symbols.
- Confirm CI: `ctest -R 'GethNaming|FiscoExecute|EthReference|OpStackExecute|TxPipeline'`.

---

### 2. Minimum deprecation window

| Symbol class | Minimum releases with `[[deprecated]]` before removal |
| --- | --- |
| Wave 1 internal aliases | 1 release after last in-tree call site migrated |
| Wave 2 kernel forwards | 1 release after Wave 1 (already deprecated since ADR-031) |
| Wave 3 `*Execute` forwards | 1 release after Wave 3 promotion lands |
| Wave 4 `*Execute` removal | 1 release after Wave 3 |

---

### 3. Gate checklist (required before each wave PR)

**All waves:**

- [ ] `rg` monorepo for symbol name returns zero non-doc, non-ADR hits (or only intentional test of deprecated forward).
- [ ] `GethNamingAliasesTest` updated; no deleted-alias cases unless testing removal PR itself.
- [ ] ADR-030 / ADR-031 / this ADR appendix date row added.
- [ ] No new Tier E symbols introduced without ADR.

**Wave 3–4 additional:**

- [ ] TE migration checklist (§4) complete.
- [ ] `transaction-executor` tests green (`CompatExecuteViaHost*`, OP fixture).
- [ ] Downstream services (`bcos-executor`, Tars executor) audited for direct `*Execute` includes.

---

## Transaction executor migration checklist

TE enters execution only through chain L1 adapters (not kernel Tier E):

```text
TransactionExecutorImpl           → applyFiscoMessage      (was fiscoExecute)
EthTransactionExecutorImpl        → applyReferenceMessage  (was ethReferenceExecute)
OpStackTransactionExecutorImpl    → applyOpStackMessage    (was opStackExecute)
```

| Step | Status (2026-06-30) | Wave | Action |
| --- | --- | --- | --- |
| TE FISCO execute path calls `applyFiscoMessage` | ✅ P2 | — | `TransactionExecutorImpl.h` `fiscoExecuteTx()` |
| TE ETH reference path calls `applyReferenceMessage` | ✅ P2 | — | `EthTransactionExecutorImpl.h` |
| TE OP path calls `applyOpStackMessage` | ✅ P2 | — | `OpStackTransactionExecutorImpl.h` |
| TE compat tests use `applyFiscoMessage` | ✅ | — | `ExecuteViaHostCompatTest.cpp`, harness headers |
| Rename local helpers `*ExecuteTx()` → `*ApplyMessage()` (optional hygiene) | ☐ | 3 | Comments only; not blocking |
| TE comments / log strings cite `apply*Message` not `*Execute` | ☐ | 3 | Search `transaction-executor/` |
| TE includes only canonical headers after Wave 3 | ☐ | 4 | No `#include` dependency on deprecated forward |
| TE release notes document `*Execute` removal | ☐ | 4 | One release before Wave 4 |

TE **never** required migration for `runTxPipeline` / `executeMessage` (chain adapters call `stateTransitionExecute` / `innerExecute` internally since ADR-031).

---

## bcos-evm internal migration checklist

| Step | Status (2026-06-30) | Wave | Action |
| --- | --- | --- | --- |
| Chain bridges call `stateTransitionExecute` | ✅ ADR-031 | — | `FiscoExecute.cpp`, `EthReferenceExecute.cpp`, OP lifecycle |
| `ChainPrecheckPolicy::pipelineInvokeEvmKernel` default calls `innerExecute` | ✅ ADR-031 | — | |
| bcos-evm production code avoids `runTxPipeline` / `executeMessage` | ☐ audit | 2 | `rg` `bcos-evm/{bcos,eth,opstack}` excluding headers/tests |
| Tests prefer canonical names; deprecated coverage in `GethNamingAliasesTest` | ✅ partial | 1–2 | Expand as Wave 1 aliases drop |
| OP `lifecycleCheckEntryRules` used in production; `checkEntryRules` test-only or gone | ☐ | 1 | `OpStackTxLifecycle.cpp` |
| `debitIntrinsicGas` absent from production paths | ☐ | 1 | Prefer `deductIntrinsicGas` |
| `runExecutionFrame` absent from production paths | ☐ | 1 | Prefer `runCallFrame` or `evmCall` aliases |

---

## Consequences

- Tier E removal is **sequenced and gated**; P5 documents the plan; P6+ executes waves.
- Wave 1 can proceed independently of TE once bcos-evm grep is clean.
- Wave 3–4 are the only TE-blocking removals; P2 already satisfied the syscall boundary.
- CI keeps deprecated-forward tests until the wave that removes the symbol merges.

---

## Appendix — Timeline (planned)

| Date / phase | Event |
| --- | --- |
| 2026-06-29 | ADR-030: Tier E stable ABI defined |
| 2026-06-30 | ADR-031 P1: `stateTransitionExecute` / `innerExecute` canonical; kernel Tier E deprecated |
| 2026-06-30 | P2: TE prefers `apply*Message`; `*Execute` still exported |
| 2026-06-30 | P4: `lifecycleCheckEntryRules` canonical; `checkEntryRules` deprecated |
| 2026-06-30 | **ADR-032 P5:** retirement schedule accepted (this document) |
| TBD Wave 1 | Remove internal transitional aliases (§1 Wave 1) |
| TBD Wave 2 | Remove `runTxPipeline` / `executeMessage` forwards |
| TBD Wave 3 | Promote `apply*Message` to exported symbols; deprecate `*Execute` |
| TBD Wave 4 | Remove `*Execute` forwards |
| TBD Wave 5 | Doc + aggregate header cleanup |

---

## Appendix — Quick reference: Tier E inventory

| Tier E symbol | Canonical | Deprecated since | Removal wave |
| --- | --- | --- | --- |
| `runTxPipeline` | `stateTransitionExecute` | ADR-031 (2026-06-30) | 2 |
| `executeMessage` | `innerExecute` | ADR-031 (2026-06-30) | 2 |
| `fiscoExecute` | `applyFiscoMessage` | TBD (Wave 3) | 4 |
| `ethReferenceExecute` | `applyReferenceMessage` | TBD (Wave 3) | 4 |
| `opStackExecute` | `applyOpStackMessage` | TBD (Wave 3) | 4 |
| `debitIntrinsicGas` | `deductIntrinsicGas` | Phase 3 batch 1 | 1 |
| `runExecutionFrame` | `runCallFrame` | ADR-029 L4 | 1 |
| `checkEntryRules` | `lifecycleCheckEntryRules` | P4 (2026-06-30) | 1 |
| `buildExecuteMessageInput` | `EvmTxContextView` | ADR-027 | 1 |
| `dispatchPrecompile` | envelope API | ADR-024 | 1 |
| ChainPrecheckPolicy legacy virtuals | `pipeline*` | ADR-029 | 1 |
| `TxExecutionRunner::run` | `runEvmKernelTopLevel` | ADR-029 L3 | 1 |
