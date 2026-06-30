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
3. Preserve **stable external ABI** (`executeMessage`, `fiscoExecute`, …) via aliases until TE migrates.

**Non-goals (v1):**

- Mass rename of all types/files in a single PR.
- Replacing ADR-029 layer prefixes in code immediately (both vocabularies coexist; comments and docs use §3).
- 1:1 file layout parity with geth (`core/` vs `eth/pipeline/`).

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
| **E — stable ABI** | Keep until TE explicitly migrates | `executeMessage`, `fiscoExecute` |

New **eth kernel** code: use Tier A/B names in function and log strings. Tier E remains as `inline` forwarders or documented aliases.

### 2. Entry points (Tier C + E)

| geth | bcos-evm (current) | ADR-030 canonical (chain) | Stable ABI (retain) |
| --- | --- | --- | --- |
| `ApplyMessage` | `ethReferenceExecute` | `applyReferenceMessage` | `ethReferenceExecute` |
| `ApplyMessage` | `fiscoExecute` | `applyFiscoMessage` | `fiscoExecute` |
| `ApplyMessage` + op lifecycle | `opStackExecute` → `runOpStackTxLifecycle` | `applyOpStackMessage` | `opStackExecute` |

**Reading rule:** `*Execute` in code = **ApplyMessage adapter** for that chain, not “run EVM” generically.

### 3. `stateTransition.execute` step map (Tier A)

Maps **current ADR-029 code** (as of 2026-06-29) to geth. Use the **geth column** in comments when touching these functions.

| Order | geth (`state_transition.go`) | bcos-evm (current) | ADR-030 target name (eth kernel) |
| --- | --- | --- | --- |
| — | `newStateTransition` | `TxPipelineContext` ctor + bridge setup | `StateTransition` context object |
| 1 | `preCheck` (incl. `buyGas` on ETH) | `pipelineCheckRules`, `pipelineCheckGasAffordable`, OP `lifecycleCheckEntryRules` | `preCheck` (+ chain hooks inside) |
| 2 | `IntrinsicGas` + `Charge` | `deductIntrinsicGas` | `deductIntrinsicGas` (Phase 3 batch 1 ✅) |
| 3 | `FloorDataGas` | Eip7623 mode in `deductIntrinsicGas` + `captureSettlementSnapshot` | `checkFloorDataGas` / `floorDataGas` |
| 4 | `CanTransfer` | `pipelineCheckBalance` | `canTransfer` (`CanTransfer.h`) + `preCheckCanTransfer` (pipeline slice) |
| 5 | `state.Prepare` | `warmTransactionEntry`, transient clear in `TxExecutionRunner` | `prepareState` |
| 6 | `evm.Create` / `evm.Call` | `pipelineInvokeEvmKernel` → `executeMessage` → `runEvmKernelTopLevel` | `innerExecute` |
| 7 | post-execution refund / 7623 uplift | `captureSettlementSnapshot`, `onPostExecuteNormalize` | `finalizeGasUsed` (geth: end of `execute`) |
| — | `execute()` wrapper | `runTxPipeline` | `StateTransition::execute` (optional rename of pipeline driver) |

**`pipelineSetupMessage`:** FISCO CREATE derivation / message normalization — maps to geth `TransactionToMessage` + chain address rules, **before** `stateTransition`; keep as `normalizeMessage` or chain hook, not a geth `execute` step.

**`buyGas` / `refundGas`:** geth `stateTransition.buyGas` inside `preCheck`. In bcos-evm:

| Chain | geth analogue | Location |
| --- | --- | --- |
| ETH reference | part of `preCheck` / fee ledger | `EthTxFeeSettlement`, orchestration |
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
| `OrchestrationProfile` | `preCheck` hook table | `StateTransitionHooks` | chain `preCheck` / error mapping |
| `BindingsContext` | — | `HookBindInputs` | inputs to build hooks |
| `ChainPrecheckPolicy` | `preCheck` slices | `PreCheckPolicy` | virtual preCheck steps |
| `OrchestrationErrorPolicy` | `execute` return `error` vs vmerr | `ExecutionResultMapper` | included-tx vs reject |
| `EvmHostHooks` | host hooks inside `evm.Call` | `EvmHostHooks` | in-call semantics |
| `AuthPort` | — | `AuthCheck` (FISCO) | ADR-017 |
| `ChainCallTargetDispatcher` | `ActivePrecompiles` + dispatch | `ChainPrecompileDispatch` | ADR-024 |
| `runOpStackTxLifecycle` | op-geth outer `execute` | `OpStackMessageApplier` | gasPool, settle, deposit |
| `OpStackSettlementFacade` | — | `SettlementProjection` | read-only fee view |

### 7. ADR-029 ↔ ADR-030 coexistence

| ADR-029 (layer prefix) | ADR-030 (geth) | When to use which |
| --- | --- | --- |
| `pipelineCheckRules` | `preCheck` (rules slice) | Code/logs migrating to geth: prefer `preCheck`; layer docs: `pipeline*` |
| `pipelineInvokeEvmKernel` | `innerExecute` | Same |
| `runEvmKernelTopLevel` | `innerExecute` body / post-`Prepare` | Same |
| `runCallFrame` | `evm.Call` | Both valid; cite geth in comment |
| `lifecycleCheckEntryRules` | op-geth pre-`innerExecute` | Keep `lifecycle*` (no geth core name) |

**Migration policy:** ADR-030 names are the **long-term target** for `eth/` kernel identifiers. ADR-029 prefixes remain valid until a rename PR lands; new comments should include both:

```cpp
// geth: stateTransition.preCheck — ADR-030
void FiscoPrecheckPolicy::pipelineCheckRules(TxPipelineContext& ctx) const
```

### 8. Stable aliases (Tier E — do not break without TE ADR)

| Stable symbol | Forwards to (current) | ADR-030 canonical |
| --- | --- | --- |
| `executeMessage` | `TxExecutionRunner::runEvmKernelTopLevel` | `innerExecute` / `applyMessage` kernel |
| `fiscoExecute` | FISCO bridge + `runTxPipeline` | `applyFiscoMessage` |
| `ethReferenceExecute` | Eth bridge + `runTxPipeline` | `applyReferenceMessage` |
| `opStackExecute` | `runOpStackTxLifecycle` | `applyOpStackMessage` |
| `runTxPipeline` | L2 driver | `StateTransition::execute` |
| `runExecutionFrame` | deprecated → `runCallFrame` | `evmCall` / `evmCreate` |

Optional type aliases — **not used in code**; keep canonical C++ names (`ExecuteMessageOutput`, `TxPipelineContext`). geth prose map in §5 / Appendix A.

```cpp
// ADR-030 documentation only (no header alias):
// geth ExecutionResult  ↔ ExecuteMessageOutput
// geth stateTransition  ↔ TxPipelineContext
```

Tier A inline aliases (implemented 2026-06-29, coexist with ADR-029 `pipeline*`):

| geth / ADR-030 | Alias symbol | Forwards to |
| --- | --- | --- |
| `preCheck` slices | `preCheckRules`, `preCheckGasAffordable`, `preCheckCanTransfer` | `pipelineCheck*` on `ChainPrecheckPolicy` |
| `normalizeMessage` | `normalizeMessage` | `pipelineSetupMessage` |
| `innerExecute` | `innerExecute` | `pipelineInvokeEvmKernel` / `executeMessage` |
| `IntrinsicGas` | `deductIntrinsicGas` | canonical (`IntrinsicGasDebit.h`) |
| `IntrinsicGas` (deprecated) | `debitIntrinsicGas` | `deductIntrinsicGas` |
| `state.Prepare` | `prepareState` | `warmTransactionEntry` |
| `execute` | `stateTransitionExecute` | `runTxPipeline` |
| `finalizeGasUsed` | `finalizeGasUsed` | `onPostExecuteNormalize` |
| `ApplyMessage` | `applyReferenceMessage`, `applyFiscoMessage`, `applyOpStackMessage` | `*Execute` |
| `evm.Call` / `Create` | `evmCall`, `evmCreate`, `evmDelegateCall`, `evmStaticCall` | `runCallFrame` |
| Bridge driver | `stateTransitionExecute` | `runTxPipeline` (used in `*ExecutionBridge` / lifecycle) |

### 9. Parity reading guide

When reviewing geth parity, walk this checklist in order:

1. **Reject vs included** — geth `preCheck` / intrinsic failure → `return nil, err` vs bcos `earlyExit` + `OrchestrationErrorPolicy` (see error-handling parity reports).
2. **`preCheck`** — map `pipelineCheck*` + OP `lifecycleCheckEntryRules`.
3. **`IntrinsicGas` / `FloorDataGas`** — map `deductIntrinsicGas` + `IntrinsicDebitMode`.
4. **`CanTransfer`** — map `pipelineCheckBalance`.
5. **`Prepare`** — map warm + transient + 7702 auth apply timing vs geth `execute` block.
6. **`innerExecute`** — map `executeMessage` path through `runCallFrame`.
7. **Nested calls** — map `EthHost::call` to recursive `evm.Call`.
8. **OP only** — map lifecycle to op-geth `innerExecute` wrapper + `GasPool`.

---

## Consequences

- Architecture docs and PR descriptions should use **geth names** when discussing portable semantics, and **chain suffix** for FISCO/OP-only behavior.
- ADR-029 remains the code-level phase-prefix standard until refactors; ADR-030 is the **geth Rosetta stone**.
- New tests that cite geth should name cases after geth steps (`preCheck_rejects`, `intrinsic_gas_OOG`, `prepare_warms_precompile`, `innerExecute_call`).
- Renames follow Tier A/B first (`eth/`), then Tier C chain entry points; Tier E aliases removed only with TE migration plan.

---

## Compliance checklist

- [x] New `eth/` pipeline/kernel functions: comment with `geth: <symbol>` (§3–4).
- [x] New tests that cite geth name cases after geth steps where applicable (`GethNamingAliasesTest`, TxPipeline geth comments).
- [ ] Parity PR description lists geth file:line anchor alongside bcos-evm symbol.
- [ ] Chain-only behavior labeled extension in §6, not claimed as geth parity without op-geth cite.
- [x] No removal of Tier E symbols without explicit TE/ADR follow-up.
- [x] `architecture-overview.md` flow diagrams dual-label critical steps (ADR-029 + geth) when updated.

---

## Appendix A — Quick lookup table

| If you are in… | Think geth… |
| --- | --- |
| `fiscoExecute` / `ethReferenceExecute` | `ApplyMessage` |
| `runOpStackTxLifecycle` | `ApplyMessage` + op-geth outer `execute` |
| `TxPipelineContext` | `stateTransition` fields |
| `runTxPipeline` | `stateTransition.execute` |
| `pipelineCheck*` | `preCheck` |
| `deductIntrinsicGas` | `IntrinsicGas` + `Charge` |
| `pipelineCheckBalance` | `CanTransfer` |
| `warmTransactionEntry` | `state.Prepare` |
| `executeMessage` | after `Prepare`: `evm.Call`/`Create` |
| `runCallFrame` | `evm.Call` / `Create` |
| `EthHost::call` | nested `evm.Call` |
| `ExecutionBundle` / `ExecutionSession` | `SetTxContext` + host injection (no geth type) |
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
