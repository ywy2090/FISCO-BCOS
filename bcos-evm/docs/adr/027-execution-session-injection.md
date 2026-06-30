# ADR-027: ExecutionSession Injection (Chain Bundle + Kernel View)

**Status:** Accepted  
**Date:** 2026-06-26  
**Related:** ADR-005, ADR-019, ADR-024, `eth/pipeline/TxPipelineContext.h`, `eth/pipeline/BuildExecuteMessageInput.h`, `eth/ExecuteMessage.h`, `eth/execution/ExecutionFrame.h`, `eth/state/EthHost.hpp`, `bcos/FiscoExecute.cpp`, `opstack/OpStackTxLifecycle.cpp`, `eth/reference/EthReferenceExecute.cpp`

---

## Context

ADR-024 §6 documents per-tx injection of `ChainCallTargetDispatcher*` and `VmHostPolicy* extension` from orchestration into `ExecuteMessageInput` → `FrameExecutionEnv` → nested `EthHost::call`. Today that wiring is **manual and multi-hop**:

```text
bridge/lifecycle → TxPipelineContext.{extension, chainPort}
                → buildExecuteMessageInput(ctx)   // ~15 field copy
                → TxExecutionRunner
                → EthHost(m_extension, m_chainPort)
                → FrameExecutionEnv.{extension, chainPort}
```

**Observed friction:**

1. **No locality for injection.** Each new port field requires edits at bridge, `TxPipelineContext`, `BuildExecuteMessageInput`, and test fixtures. Omission of `chainPort` (e.g. `nullptr` when FISCO dispatch is configured) silently routes chain targets through builtin/EVM paths.

2. **Ownership scattered in bridges.** `FiscoVmHostPolicy` and `std::optional<FiscoChainCallTargetAdapter>` live as separate stack locals in `FiscoExecute`; OpStack mirrors with `OpStackChainCallTargetAdapter`. Lifetime must span `runTxPipeline` **and** nested `EthHost::call`, but nothing groups the bundle.

3. **`OrchestrationProfile::Session` is the wrong seam.** FISCO / Eth / OpStack already have policy-binding `Session` types (precheck + error policy). Injection is a **kernel execution-environment** concern, not orchestration policy.

4. **Tests do not enforce pointer identity.** ADR-024 requires the **same** `chainPort` pointer at top-level and nested depth. Propagation tests cover 7702 authorization, not chain-port identity across `EthHost::call`.

**Non-goals (v1):**

- Removing duplicate `ctx.extension` / `ctx.chainPort` fields from `TxPipelineContext` (follow-up after `wire()` is stable).
- Changing `ExecuteMessageInput` or `FrameExecutionEnv` field shapes.
- TE `TransactionExecutorImpl` changes (injection stays inside `fiscoExecute` bridge).
- Replacing `OrchestrationProfile::Session` or merging it with execution injection.
- Prepare-phase warm (Gap 36); see separate product decision.

---

## Decision

Grilling outcomes (2026-06-26):

| # | Question | Choice |
| --- | --- | --- |
| D1 | Seam placement | **A.** New kernel module `eth/pipeline/ExecutionSession` (not construction-valid `TxPipelineContext`, not `OrchestrationProfile::Session`). |
| D2 | Lifetime | **2c.** Chain-side **Bundle** owns stack adapters/policies; kernel **View** borrows pointers. |
| D3 | View fields | **3b.** Tier 1 (injection) + Tier 2 (execution infra); Tier 3 (tx mutable) stays in `TxPipelineContext`. |
| D4 | Validation | **4c+4d+tests.** Bundle factory checks; debug dual-run after `wire()`; `ExecutionSessionPropagationTest` matrix. |
| D5 | Migration | **Single PR** — zero behavior change; existing characterization + propagation tests green. |

### 1. Field tiers

| Tier | Fields | Owner |
| --- | --- | --- |
| **1 — injection seam** | `chainPort*`, `extension*` | Chain Bundle (owned objects); View borrows |
| **2 — execution infra** | `vm*`, `blockHashes` | Set in Bundle factory from request/revision; copied via `wire()` |
| **3 — tx mutable** | `state`, `message`, `gasPrice`, `revisionConfig`, `txProps`, `authorizations`, pipeline outputs | `TxPipelineContext` only; merged in `toExecuteMessageInput(ctx)` |

FISCO legacy `fix_storage_status` / `fix_nonce_init` (from `FiscoRevisionConfig`) are **not** Tier-2 kernel fields. They configure `FiscoVmHostPolicy::RevisionFlags` and flow through Tier-1 `extension*` hook overrides (`applySstoreRefund`, `classifyStorageStatus`, `finalizeTopLevelCreateNonce`). See `docs/superpowers/specs/2026-06-30-evm-host-hooks-fisco-legacy-design.md`.

Orchestration-only overlays (`skipTopLevelSenderNonceBump`, `txHash`) remain outside the session; `ChainPrecheckPolicy::tuneExecutionInput` applies them after projection (ADR-019 unchanged).

### 2. Kernel module: `eth/pipeline/ExecutionSession.h`

```cpp
namespace bcos::evm {

struct EvmTxContextView {
    ChainCallTargetDispatcher* chainPort{nullptr};
    state::EvmHostHooks* extension{nullptr};
    evmc::VM* vm{nullptr};
    state::BlockHashes blockHashes{};

    /// Write Tier 1+2 into ctx; set ctx.session = this.
    void wire(TxPipelineContext& ctx) const;

    /// Merge Tier 3 from ctx → ExecuteMessageInput (replaces buildExecuteMessageInput).
    ExecuteMessageInput toExecuteMessageInput(TxPipelineContext const& ctx) const;
};

}  // namespace bcos::evm
```

**`wire()` invariants:**

- `vm != nullptr` → `throw std::invalid_argument` (same as `runTxPipeline` today).
- Sets `ctx.extension`, `ctx.chainPort`, `ctx.inputs.vm`, and `ctx.session = this`.
- `#ifndef NDEBUG`: assert wired values match session fields (dual-run vs manual assignment during migration).

**`toExecuteMessageInput(ctx)`:** copies Tier 3 from `TxPipelineContext` / `ctx.inputs` per current `BuildExecuteMessageInput` semantics; does not mutate `ctx`.

**Seam discipline (ADR-005 Rule 1):** `ExecutionSession.h` must not `#include` `bcos/` or `opstack/`. Chain types stay in Bundle headers.

### 3. Chain-side Bundles (ownership)

| Bundle | Location | Owns | `chainPort` in view |
| --- | --- | --- | --- |
| `EthExecutionBundle` | `eth/reference/` | `EthVmHostPolicy` | `nullptr` (valid) |
| `FiscoExecutionBundle` | `bcos/` | `FiscoVmHostPolicy`, `optional<FiscoChainCallTargetAdapter>` | non-null iff `input.chainDispatchPort != nullptr` |
| `OpStackExecutionBundle` | `opstack/` | `OpStackChainCallTargetAdapter` | always non-null |

Construction order unchanged: `TxPipelineContext` first (Bundle needs `&ctx.state`), then Bundle, then `session.wire(ctx)`.

```cpp
// FISCO (illustrative)
TxPipelineContext ctx{...};
FiscoExecutionBundle bundle{ctx, input};
ExecutionSession const& session = bundle.view();
session.wire(ctx);
runTxPipeline(ctx, ...);
```

Bundle exposes `ExecutionSession const& view() const noexcept`; view lifetime ≤ Bundle ≤ `runTxPipeline` + nested calls.

### 4. Pipeline integration

`TxPipeline.cpp` step ⑥:

```cpp
auto executeInput = ctx.session->toExecuteMessageInput(ctx);
precheckPolicy.tuneExecutionInput(executeInput);
```

`BuildExecuteMessageInput.h` becomes `[[deprecated]]` inline forward to `ctx.session->toExecuteMessageInput(ctx)` until call sites are removed.

`TxPipelineContext` gains:

```cpp
ExecutionSession const* session{nullptr};
```

Bridges must set `session` via `wire()` before `runTxPipeline`.

### 5. Validation (D4)

| Layer | Check |
| --- | --- |
| Kernel `wire()` | `vm != nullptr` |
| `OpStackExecutionBundle` | debug `assert` + release `throw` if `chainPort` would be null |
| `FiscoExecutionBundle` | if `chainDispatchPort != nullptr`, adapter must be engaged; debug assert |
| `EthExecutionBundle` | no assert on null `chainPort` |
| Debug dual-run | after `wire()`, `ctx.chainPort == session.chainPort` (and extension) |
| Tests | see §7 |

Release behavior for FISCO/Eth when mis-wired: **unchanged** (silent); OpStack mis-wire is a hard fail (production path must never omit adapter).

### 6. Relationship to ADR-024 §6

ADR-024 §6 injection table remains normative for **what** is injected. ADR-027 specifies **how**:

| ADR-024 requirement | ADR-027 enforcement |
| --- | --- |
| Same `chainPort` top-level and nested | `ExecutionSession` single source; `EthHost` reads `m_chainPort` from session-wired input |
| Per-`executeViaHost` adapter lifetime | Chain Bundle stack ownership |
| Eth `nullptr` port | `EthExecutionBundle` view |

Update `architecture-overview.md` §6 injection diagram when implementation lands.

### 7. Test surface

| Test module | Interface |
| --- | --- |
| `ExecutionSessionPropagationTest` | `wire()` + nested `EthHost::call`: top-level and depth-1 share `chainPort` address |
| Eth row | `chainPort == nullptr` smoke |
| OpStack row | L1 predeploy / empty-code hook hits chain dispatch |
| FISCO row | with `chainDispatchPort`; nullable smoke without dispatch |
| Existing | `CallTargetCharacterizationTest`, 7702 propagation tests — must stay green (zero behavior change gate) |

---

## Migration (single PR)

**Status:** Implemented 2026-06-26.

| Step | Deliverable | Status |
| --- | --- | --- |
| 1 | `ExecutionSession.h` + unit tests | Done |
| 2 | `EthExecutionBundle`, `FiscoExecutionBundle`, `OpStackExecutionBundle` | Done |
| 3 | Rewire three bridges / lifecycle entry points | Done |
| 4 | `TxPipelineContext.session`; `TxPipeline.cpp` uses `ctx.session->toExecuteMessageInput` | Done |
| 5 | Deprecate `BuildExecuteMessageInput.h` forwarder | Done |
| 6 | `ExecutionSessionPropagationTest` + debug dual-run in `wire()` | Done |
| 7 | `architecture-overview.md` injection section | Done |

**Behavior change:** None intended. Merge gate = full existing bcos-evm ctest relevant targets + new propagation tests.

---

## Consequences

**Positive:**

- Injection **locality** at Bundle factory + `ExecutionSession::wire`.
- **Leverage:** one projection API for three entry paths; new ports extend Bundle + session fields, not six hop sites.
- **Interface is test surface** for ADR-024 pointer-identity invariant.

**Costs:**

- Transient duplication: `ctx.extension` / `ctx.chainPort` remain until a follow-up deletes them.
- Three Bundle types (justified: ADR-005 forbids chain types in kernel).

**Supersedes:** Manual injection narrative in architecture review candidate #7 (ExecutionSession RAII); implementation was Speculative, now Accepted.

---

## Compliance checklist

- [x] `ExecutionSession.h` has no `#include` of `bcos/` or `opstack/`.
- [x] All three entry paths construct a Bundle and call `wire()` before `runTxPipeline`.
- [x] `ctx.session != nullptr` when pipeline reaches step ⑥.
- [x] `toExecuteMessageInput` matches deprecated `buildExecuteMessageInput` for all fields (parity test).
- [x] OpStack: null `chainPort` fails in release (`OpStackExecutionBundle` ctor).
- [x] `ExecutionSessionPropagationTest` green on Eth / OpStack rows.
- [x] `architecture-overview.md` updated; ADR-024 §6 references ADR-027.

---

## Naming follow-up (2026-06-26)

Grilling outcome: disambiguate orchestration policy bind input from kernel `ExecutionSession`.

| Before | After |
| --- | --- |
| `OrchestrationProfile::Session` | `OrchestrationProfile::BindingsContext` |
| local variable `session` | `bindingsCtx` |
| FISCO `BindingsContext.extension` | **removed** (dead; injection via `ExecutionBundle`) |

**Unchanged:** `BindingsContext` and `ExecutionSession` remain **separate seams** — not merged (ADR-027 non-goal).

Bridge pattern (all three entry paths):

```text
*ExecutionBundle{ctx, input}   // wire() → ctx.session
BindingsContext bindingsCtx      // Profile::bind input only
Profile::bind(bindingsCtx) → runTxPipeline(ctx, ...)
```

---

## References

- ADR-024 §6 (injection wiring table)
- ADR-019 Q14 (`ctx.state` ownership; `buildExecuteMessageInput` single-point assembly — superseded by `ExecutionSession::toExecuteMessageInput`)
- ADR-005 Rule 1 (kernel must not include chain headers)
- Architecture review 2026-06-26 — candidate #7 ExecutionSession injection RAII
