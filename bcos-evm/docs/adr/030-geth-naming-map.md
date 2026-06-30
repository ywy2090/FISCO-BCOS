# ADR-030: geth Naming Map (bcos-evm ↔ go-ethereum)

**Status:** Accepted  
**Date:** 2026-06-29  
**Related:** ADR-005, ADR-019, ADR-023, ADR-027, ADR-029, `architecture-overview.md`, `bcos-evm/docs/adr/bcos-evm-vs-geth-comparison.md`

**Reference implementation (pin):** `blockchain-impl/go-ethereum` @ v1.17.3 — `core/state_transition.go`, `core/vm/evm.go`  
**OP extension pin:** `blockchain-impl/op-geth` — same paths + `innerExecute`

---

## Context

`bcos-evm` uses FISCO-specific vocabulary (`Bridge`, `ExecutionSession`, `OrchestrationProfile`, `pipeline*`) that does not line up with go-ethereum’s state-transition model. Reviewers and parity work repeatedly re-derive the same mapping against `ApplyMessage` / `stateTransition.execute`.

ADR-029 introduced **layer prefixes** (`pipeline*`, `runEvmKernel*`, `runCallFrame`) so call stacks encode phase. ADR-030 adds a **geth vocabulary layer**: every major symbol has a documented geth analogue for reading code, writing tests, and naming **future** refactors.

**Goals:**

1. One canonical table: bcos-evm symbol → geth symbol → file anchor.
2. Distinguish **portable eth kernel** (should use geth names when we rename) vs **chain extensions** (keep chain suffix).
3. ~~Preserve stable external ABI~~ — **Tier E retired ADR-032 (2026-06-30)**; new code uses canonical names (`apply*Message`, `stateTransitionExecute`, `innerExecute`).

**Non-goals (v1):**

- Mass rename of all types/files in a single PR.
- Replacing ADR-029 layer prefixes in code immediately (both vocabularies coexist; comments and docs use §3).
- 1:1 file layout parity with geth (`core/` vs `eth/kernel/state-transition/`).

---

## geth anchor flow

```text
ApplyMessage(evm, msg, gp)                    // core/state_transition.go
  └─ stateTransition.execute()
       ├─ preCheck()                         // nonce, balance, buyGas, typed-tx rules
       ├─ IntrinsicGas + gasRemaining.Charge // intrinsic debit
       ├─ FloorDataGas                       // Prague EIP-7623 (when enabled)
       ├─ CanTransfer (top-level value)      // ErrInsufficientFundsForTransfer
       ├─ state.Prepare(...)                 // access list, transient reset, precompile warm
       └─ evm.Create / evm.Call              // core/vm/evm.go
            └─ interpreter / nested Call
```

**op-geth:** `execute()` may delegate to `innerExecute()` after deposit/system-tx branches; `GasPool` + `buyGas` wrap the portable path.

---

## Decision

### 1. Canonical naming tiers

| Tier | Rule | Example |
| --- | --- | --- |
| **A — geth portable** | Prefer geth name in `eth/` for steps that exist in `stateTransition.execute` | `preCheck`, `intrinsicGas`, `prepareState`, `innerExecute` |
| **B — geth VM** | Align frame entry with `evm.Call` / `Create` | `evmCall`, `evmCreate`, nested `evmCall` |
| **C — chain extension** | `apply{Chain}Message` + lifecycle/settlement; not forced into geth names | `applyOpStackMessage`, `lifecycleCheckEntryRules` |
| **D — FISCO injection** | Document as extension; no geth symbol | `AuthPort`, `EvmHostContext` bundle |
| **E — stable ABI** | ~~Retained until TE migrates~~ **retired ADR-032 Waves 2–4 (2026-06-30)** | ~~`executeMessage`, `fiscoExecute`~~ → canonical only |

New **eth kernel** code: use Tier A/B names in function and log strings. ~~Tier E~~ — **removed**; see ADR-032 §8.

### 2. Entry points (Tier C — canonical since ADR-032 Wave 3)

| geth | bcos-evm (canonical) | Header | Removed Tier E (Wave) |
| --- | --- | --- | --- |
| `ApplyMessage` | `applyEthMessage` | `eth/apply/EthMessage.h` | ~~`ethReferenceExecute`~~ (4, 2026-06-30) |
| `ApplyMessage` | `applyFiscoMessage` | `bcos/ApplyFiscoMessage.h` | ~~`fiscoExecute`~~ (4, 2026-06-30) |
| `ApplyMessage` + op lifecycle | `applyOpStackMessage` → `runOpStackTxLifecycle` | `opstack/ApplyOpStackMessage.h` | ~~`opStackExecute`~~ (4, 2026-06-30) |

**Reading rule:** chain L1 entry = **ApplyMessage adapter** for that chain (`apply*Message`), not “run EVM” generically.

### 3. `stateTransition.execute` step map (Tier A)

Maps **current ADR-029 code** (as of 2026-06-29) to geth. Use the **geth column** in comments when touching these functions.

| Order | geth (`state_transition.go`) | bcos-evm (current) | ADR-030 target name (eth kernel) |
| --- | --- | --- | --- |
| — | `newStateTransition` | `TxPipelineContext` ctor + bridge setup | `StateTransition` context object |
| 1 | `preCheck` (incl. `buyGas` on ETH) | `onPreCheckRules`, `onPreCheckGasAffordable`, OP `lifecycleCheckEntryRules` | `preCheck` (+ chain hooks inside) |
| 2 | `IntrinsicGas` + `Charge` | `deductIntrinsicGas` | `deductIntrinsicGas` (Phase 3 batch 1 ✅) |
| 3 | `FloorDataGas` | Eip7623 mode in `deductIntrinsicGas` + `captureSettlementSnapshot` | `checkFloorDataGas` / `floorDataGas` |
| 4 | `CanTransfer` | `onPreCheckCanTransfer` | `canTransfer` (`CanTransfer.h`) + `onPreCheckCanTransfer` (pipeline slice) |
| 5 | `state.Prepare` | `warmTransactionEntry`, transient clear in `TxExecutionRunner` | `prepareState` |
| 6 | `evm.Create` / `evm.Call` | `onInvokeInnerExecute` → `innerExecute` → `runEvmKernelTopLevel` | `innerExecute` |
| 7 | post-execution refund / 7623 uplift | `captureSettlementSnapshot`, `onFinalizeGasUsed` | `onFinalizeGasUsed` (geth: end of `execute`) |
| — | `execute()` wrapper | `stateTransitionExecute` | `StateTransition::execute` (canonical pipeline driver) |

**`onNormalizeMessage`:** FISCO CREATE derivation / message normalization — maps to geth `TransactionToMessage` + chain address rules, **before** `stateTransition`; keep as `onNormalizeMessage` or chain hook, not a geth `execute` step.

**`buyGas` / `refundGas`:** geth `stateTransition.buyGas` inside `preCheck`. In bcos-evm:

| Chain | geth analogue | Location |
| --- | --- | --- |
| ETH reference | part of `preCheck` / fee ledger | `eth/apply/EthTxFeeSettlement.h`, orchestration |
| FISCO | TE + bridge | `TransactionExecutorImpl`, not always inside pipeline |
| OP Stack | `preCheck` + async settlement | `OpStackNormalTxFeeCoordinator::buyGas`, lifecycle |

### 4. VM / frame map (Tier B)

| geth (`core/vm/evm.go`) | bcos-evm (current) | ADR-030 target |
| --- | --- | --- |
| `evm.Call` | `runCallFrame` (CALL) | `evmCall` |
| `evm.Create` / `Create2` | `runCallFrame` (CREATE) | `evmCreate` |
| `evm.DelegateCall` / `StaticCall` | `runCallFrame` | `evmDelegateCall` / `evmStaticCall` |
| Nested `evm.Call` | `EthHost::call` → `runCallFrame(Nested)` | nested `evmCall` |
| `evm.SetTxContext` | `buildTxContext` + `EthHost` | `setTxContext` / `EvmTxContext` |

ADR-029 name `runCallFrame` is **acceptable**; geth comment alias: `// geth: evm.Call`.

### 5. Types and results

| geth | bcos-evm (current) | ADR-030 documentation name | Notes |
| --- | --- | --- | --- |
| `Message` | fields in `*ExecutionRequest` + `evmc_message` | `TxMessage` | geth `Message` struct |
| `stateTransition` | `TxPipelineContext` | `StateTransition` | holds `msg`, gas, `state`, `evm` refs |
| `ExecutionResult` | `ExecuteMessageOutput` + `EVMCResult` | `ExecutionResult` | geth: `UsedGas`, `ReturnData`, `Err` |
| `GasPool` | OP block gas pool hooks | `GasPool` | already used in OP docs |
| `StateDB` | `state::State` | `StateDB` in prose; `State` in C++ API | journal + snapshot |
| `vm.StateDB.Prepare` | `warmTransactionEntry` + transient | `prepareState` | |

**`EVMCResult::status` vs `status_code`:** geth separates `error` (consensus / reject) from `vmerr` (included execution failure). bcos-evm adds `protocol::TransactionStatus` on `EVMCResult` for FISCO receipt/executor — document as **FISCO extension**, not geth `ExecutionResult.Err`.

### 6. FISCO / OP extensions (Tier C/D — no geth force-fit)

| bcos-evm concept | geth | ADR-030 name | Rationale |
| --- | --- | --- | --- |
| `FiscoExecute` | — | `FiscoMessageApplier` (doc) | ApplyMessage + FISCO fields |
| `ExecutionBundle` | — | `EvmHostContext` / `ChainHostBundle` | owns `EvmHostHooks` + adapter lifetime |
| `ExecutionSession` *(renamed ADR-030)* | `evm.SetTxContext` + host injection | **`EvmTxContextView`** | borrows into `TxPipelineContext.txContextView` |
| `OrchestrationProfile` | `preCheck` hook table | `OrchestrationBindProfile` | chain hook bind table |
| `BindingsContext` | — | `HookBindInputs` | inputs to build hooks |
| `StateTransitionHooks` | `preCheck` slices + `innerExecute` | `StateTransitionHooks` | portable hook interface |
| `StateTransitionErrorPolicy` | `execute` return `error` vs vmerr | `ExecutionResultMapper` | included-tx vs reject |
| `EvmHostHooks` | host hooks inside `evm.Call` | `EvmHostHooks` | in-call semantics |
| `AuthPort` | — | `AuthCheck` (FISCO) | ADR-017 |
| `ChainExtendedPrecompileDispatch` | `ActivePrecompiles` + dispatch | `ChainExtendedPrecompileDispatch` | ADR-024 |
| `runOpStackTxLifecycle` | op-geth outer `execute` | `OpStackMessageApplier` | gasPool, settle, deposit |
| `OpStackSettlementFacade` | — | `SettlementProjection` | read-only fee view |

### 7. ADR-029 ↔ ADR-030 coexistence

| ADR-029 (layer prefix) | ADR-030 (geth) | When to use which |
| --- | --- | --- |
| `onPreCheckRules` | `preCheck` (rules slice) | Code/logs migrating to geth: prefer `preCheck`; layer docs: `pipeline*` |
| `onInvokeInnerExecute` | `innerExecute` | Same |
| `runEvmKernelTopLevel` | `innerExecute` body / post-`Prepare` | Same |
| `runCallFrame` | `evm.Call` | Both valid; cite geth in comment |
| `lifecycleCheckEntryRules` | op-geth pre-`innerExecute` | Keep `lifecycle*` (no geth core name) |

**Migration policy:** ADR-030 names are the **long-term target** for `eth/` kernel identifiers. ADR-029 prefixes remain valid until a rename PR lands; new comments should include both:

```cpp
// geth: stateTransition.preCheck — ADR-030
void FiscoPrecheckPolicy::onPreCheckRules(TxPipelineContext& ctx) const
```

### 8. Stable aliases (Tier E — **removed ADR-032 Waves 1–4, 2026-06-30**)

| ~~Stable symbol~~ | Canonical replacement | Removed |
| --- | --- | --- |
| ~~`executeMessage`~~ | `innerExecute` | Wave 2 (2026-06-30) |
| ~~`fiscoExecute`~~ | `applyFiscoMessage` | Wave 4 (2026-06-30) |
| ~~`ethReferenceExecute`~~ | `applyEthMessage` | Wave 4 (2026-06-30) |
| ~~`opStackExecute`~~ | `applyOpStackMessage` | Wave 4 (2026-06-30) |
| ~~`runTxPipeline`~~ | `stateTransitionExecute` | Wave 2 (2026-06-30) |
| ~~`runExecutionFrame`~~ | `runCallFrame` / `evmCall` | Wave 1 (2026-06-30) |
| ~~`debitIntrinsicGas`~~ | `deductIntrinsicGas` | Wave 1 (2026-06-30) |

Optional type aliases — **not used in code**; keep canonical C++ names (`ExecuteMessageOutput`, `TxPipelineContext`). geth prose map in §5 / Appendix A.

```cpp
// ADR-030 documentation only (no header alias):
// geth ExecutionResult  ↔ ExecuteMessageOutput
// geth stateTransition  ↔ TxPipelineContext
```

Tier A inline aliases (implemented 2026-06-29, coexist with ADR-029 `pipeline*`):

| geth / ADR-030 | Alias symbol | Forwards to |
| --- | --- | --- |
| `preCheck` slices | `onPreCheckRules`, `onPreCheckGasAffordable`, `onPreCheckCanTransfer` | `pipelineCheck*` on `StateTransitionHooks` |
| `onNormalizeMessage` | `onNormalizeMessage` | `onNormalizeMessage` |
| `innerExecute` | `innerExecute` | `onInvokeInnerExecute` |
| `IntrinsicGas` | `deductIntrinsicGas` | canonical (`IntrinsicGasDebit.h`) |
| `state.Prepare` | `prepareState` | `warmTransactionEntry` |
| `execute` | `stateTransitionExecute` | canonical pipeline driver (`TxPipeline.cpp`) |
| `onFinalizeGasUsed` | `onFinalizeGasUsed` | `onFinalizeGasUsed` |
| `ApplyMessage` | `applyEthMessage`, `applyFiscoMessage`, `applyOpStackMessage` | canonical chain L1 exports |
| `evm.Call` / `Create` | `evmCall`, `evmCreate`, `evmDelegateCall`, `evmStaticCall` | `runCallFrame` |

### 9. Parity reading guide

When reviewing geth parity, walk this checklist in order:

1. **Reject vs included** — geth `preCheck` / intrinsic failure → `return nil, err` vs bcos `earlyExit` + `StateTransitionErrorPolicy` (see error-handling parity reports).
2. **`preCheck`** — map `pipelineCheck*` + OP `lifecycleCheckEntryRules`.
3. **`IntrinsicGas` / `FloorDataGas`** — map `deductIntrinsicGas` + `IntrinsicDebitMode`.
4. **`CanTransfer`** — map `onPreCheckCanTransfer`.
5. **`Prepare`** — map warm + transient + 7702 auth apply timing vs geth `execute` block.
6. **`innerExecute`** — map `executeMessage` path through `runCallFrame`.
7. **Nested calls** — map `EthHost::call` to recursive `evm.Call`.
8. **OP only** — map lifecycle to op-geth `innerExecute` wrapper + `GasPool`.

**Parity PR descriptions:** When opening a geth parity PR, cite the go-ethereum anchor in the description—symbol, file path, and line (e.g. `preCheck` in `core/state_transition.go:~433`) alongside the bcos-evm symbol and path being changed. Use Appendix B as the default pin; cite op-geth for OP-only behavior. Reviewers should not re-derive the ADR-030 mapping from scratch.

---

## Consequences

- Architecture docs and PR descriptions should use **geth names** when discussing portable semantics, and **chain suffix** for FISCO/OP-only behavior.
- ADR-029 remains the code-level phase-prefix standard until refactors; ADR-030 is the **geth Rosetta stone**.
- New tests that cite geth should name cases after geth steps (`preCheck_rejects`, `intrinsic_gas_OOG`, `prepare_warms_precompile`, `innerExecute_call`).
- Renames follow Tier A/B first (`eth/`), then Tier C chain entry points. ~~Tier E~~ retired ADR-032 Waves 1–4 (2026-06-30).

---

## Compliance checklist

Phase 2 (Tasks 1–6, 2026-06-30) — closed unless noted deferred.

- [x] New `eth/` pipeline/kernel functions: comment with `geth: <symbol>` (§3–4).
- [x] `KernelCanonicalNamingTest` registered and passing; geth step case names where applicable (replaces `GethNamingAliasesTest`, 2026-06-30).
- [x] Parity PR description lists geth file:line anchor alongside bcos-evm symbol (§9 parity PR note).
- [ ] Chain-only behavior labeled extension in §6, not claimed as geth parity without op-geth cite (ongoing review discipline).
- [x] No removal of Tier E symbols without explicit TE/ADR follow-up *(ADR-032 Waves 1–4 complete; Wave 5 doc sweep)*
- [x] `architecture-overview.md` flow diagrams use canonical names (ADR-029 layer + geth vocabulary in comments).
- [x] `eth/apply/` path documented (Phase 4b); no stale `eth/reference/` in `eth/README.md`.

---

## Appendix A — Quick lookup table

| If you are in… | Think geth… |
| --- | --- |
| `applyFiscoMessage` / `applyEthMessage` / `applyOpStackMessage` | `ApplyMessage` |
| `runOpStackTxLifecycle` | `ApplyMessage` + op-geth outer `execute` |
| `TxPipelineContext` | `stateTransition` fields |
| `stateTransitionExecute` | `stateTransition.execute` |
| `pipelineCheck*` | `preCheck` |
| `deductIntrinsicGas` | `IntrinsicGas` + `Charge` |
| `onPreCheckCanTransfer` | `CanTransfer` |
| `warmTransactionEntry` | `state.Prepare` |
| `innerExecute` | after `Prepare`: `evm.Call`/`Create` |
| `runCallFrame` | `evm.Call` / `Create` |
| `EthHost::call` | nested `evm.Call` |
| `ExecutionBundle` / `EvmTxContextView` | `SetTxContext` + host injection (no geth type) |
| `buyGas` / `refundGas` | `buyGas` + settlement (chain-timed) |

---

## Appendix B — File anchors (go-ethereum v1.17.3)

| Symbol | File | Notes |
| --- | --- | --- |
| `ApplyMessage` | `core/state_transition.go` | ~308 |
| `stateTransition.execute` | `core/state_transition.go` | ~538 |
| `preCheck` | `core/state_transition.go` | ~433 |
| `buyGas` | `core/state_transition.go` | ~367 |
| `IntrinsicGas` | `core/state_transition.go` | ~71 |
| `FloorDataGas` | `core/state_transition.go` | used in `execute` Prague block |
| `state.Prepare` | called from `execute` | ~610 |
| `evm.Call` / `Create` | `core/vm/evm.go` | ~248, ~624 |
| `ExecutionResult` | `core/state_transition.go` | ~36 |

op-geth: `innerExecute` in `core/state_transition.go` (split from `execute` for deposit/system paths).
