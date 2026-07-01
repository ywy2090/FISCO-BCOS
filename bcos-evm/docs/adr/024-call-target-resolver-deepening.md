# ADR-024: Call Target Resolver Deepening

**Status:** Accepted  
**Date:** 2026-06-26  
**Last revised:** 2026-06-26 (v1.1 — subagent review patches)  
**Related:** ADR-005, ADR-017, ADR-018, ADR-019, `docs/superpowers/specs/2026-06-26-call-target-resolver-design.md`, `eth/kernel/execution/FrameTargetResolver.*`, `eth/precompiled/PrecompileRouter.*`, `eth/precompiled/PrecompileActive.h`, `eth/kernel/execution/WarmTransactionEntry.h`, `docs/superpowers/specs/2026-06-24-execution-frame-design.md`

---

## Context

A CALL to a precompile-like target today requires bouncing across four modules:

| Step | Module | Responsibility |
| --- | --- | --- |
| 1 | `FrameTargetResolver` | Address normalization (7702, CREATE, `executionAddress`) |
| 2 | `PrecompileRouter` | Classification **and** execution envelope |
| 3 | `EvmHostHooks::tryChainPrecompile` | Chain extension hook |
| 4 | `ChainPrecompilePort` (FISCO) or inline logic (OpStack) | Chain dispatch |

**Observed friction:**

1. **Classification lacks locality.** Builtin gate (`isActivePrecompile`), chain hook (`tryChainPrecompile`), 7702 delegation bypass, and DELEGATECALL policy live in `PrecompileRouter.cpp` while address work lives in `FrameTargetResolver.cpp`.

2. **Warm and dispatch are split for chain targets.** `WarmTransactionEntry` enumerates builtin precompile addresses via `forEachActivePrecompile` only. FISCO small-address precompiles and OpStack L1 predeploys dispatch at frame time but are not in that tx-entry warm set.

3. **Chain precompile seam shape is inconsistent (ADR-017 partial).** FISCO routes through `ChainPrecompilePort`; OpStack inlines L1Block / GasPriceOracle in `OpStack chain call-target adapter`.

4. **`PrecompileRouter` mixes concerns.** Envelope logic is deep; classification inside the same function is shallow.

5. **Tests follow the old seams.** No single test surface answers “address X + chain profile → kind + warmPolicy.”

**Non-goals:** EVM execution semantics, OpStack fee routing, FISCO auth. Deepens call-target taxonomy; thins `PrecompileRouter` to envelope-only.

---

## Decision

Grilling outcomes (2026-06-26):

| # | Question | Choice |
| --- | --- | --- |
| D1 | Deep module boundary | **Merge A+B only.** `CallTargetResolver` owns address resolution + classification. `PrecompileRouter` keeps envelope. |
| D2 | Warm rules | **2b+.** `CallTargetDescriptor` includes `WarmPolicy`; `enumerateTxEntryWarmTargets` shares classification engine. |
| D3 | Chain precompile seam | **3b.** Neutral `ChainExtendedPrecompileDispatch` in `eth/core/`; FISCO + OpStack adapters. Retire `tryChainPrecompile` from main path. |
| D4 | Router input | **4a.** `executePrecompileEnvelope` trusts pre-classified descriptor; no re-classification. |
| D5 | Tests | **5b.** Three test modules + one cross-chain characterization matrix. |

### 1. New deep module: `eth/kernel/execution/CallTargetResolver`

```cpp
namespace bcos::evm::execution {

enum class CallTargetKind {
    EvmContract,
    BuiltinPrecompile,
    ChainPrecompile,
    EmptyAccount,
    PolicyRejected,
};

enum class WarmPolicy {
    Never,
    TxEntryAlways,
    TxEntryIfStatic,
    FrameEntryOnly,   // CREATE warm-pin; set by resolver, not consumed by enumerate
};

struct CallTargetDescriptor {
    CallTargetKind kind;
    evmc_address   dispatchAddress;
    WarmPolicy     warmPolicy;
    evmc_message   routed;
};

CallTargetDescriptor resolveCallTarget(
    state::State&,
    bcos::evm_standard::RevisionConfig const&,
    evmc_message,
    FrameScope,
    ChainExtendedPrecompileDispatch*,
    state::EvmHostHooks* extension);  // DELEGATECALL policy; skipHostValueTransfer read by caller

void enumerateTxEntryWarmTargets(
    bcos::evm_standard::RevisionConfig const&,
    ChainExtendedPrecompileDispatch const*,
    /* invocable<evmc_address const&> */ auto&& consume);

}  // namespace bcos::evm::execution
```

**Classification moved out of `PrecompileRouter`:**

| Today in `dispatchPrecompile` | After ADR-024 |
| --- | --- |
| `emptyCode \|\| nested` → `tryChainHook` | `resolveCallTarget` → `ChainPrecompile` |
| `isActivePrecompile` | `resolveCallTarget` → `BuiltinPrecompile` |
| 7702 delegation designator → `NotApplicable` | `resolveCallTarget` → `EvmContract` |
| DELEGATECALL + `allowDelegateCallToPrecompile` | `resolveCallTarget` → `PolicyRejected` |
| checkpoint / value transfer / `finalizeEnvelope` | **Stay in envelope** |

`PrecompileDispatchOutcome::NotApplicable` retired from hot path.

### 2. Warm policy (2b+)

| Target class | `WarmPolicy` | Rationale |
| --- | --- | --- |
| Builtin precompile | `TxEntryAlways` | Same as `forEachActivePrecompile` today |
| OpStack L1Block / GasPriceOracle | `TxEntryIfStatic` | Fixed predeploys via adapter enumerate |
| FISCO direct small-address | No range warm | Open `>= 0x1000` space |
| FISCO `[PRECOMPILED]` proxy | Proxy via `warmDestination`; resolved target not tx-entry warm | Frame-time state read |
| Ordinary EVM contract | `Never` | Unchanged |

`warmTransactionEntry` replaces the direct `forEachActivePrecompile` call with `enumerateTxEntryWarmTargets`. Sender / destination / coinbase / access-list sub-steps are unchanged; only the **source** of precompile-address warming changes.

### 3. Neutral chain port: `eth/core/ChainExtendedPrecompileDispatch`

Extends ADR-017. Interface in kernel-neutral location; FISCO implementation stays in `transaction-executor` / `bcos-executor`.

```cpp
namespace bcos::evm {

struct ChainExtendedPrecompileDispatch {
    virtual ~ChainExtendedPrecompileDispatch() = default;

    /// C7: scope required — chain hook runs when emptyCode || scope == Nested.
    virtual std::optional<execution::CallTargetDescriptor> classifyTarget(
        state::State&,
        evmc_address const& executionAddress,
        evmc_message const& msg,
        execution::FrameScope scope) = 0;

    virtual std::optional<evmc_result> dispatch(
        evmc_revision, evmc_message const& msg) = 0;

    virtual void forEachStaticWarmTarget(
        std::invocable<evmc_address const&> auto&& consume) const = 0;
};

}  // namespace bcos::evm
```

**FISCO `classifyTarget` normative rule:** `executionAddress` (from `resolveFrameTarget`) is the primary key. Dynamic `[PRECOMPILED]` proxy: read `state.get_code(executionAddress)`, parse embedded target into `dispatchAddress`. Do not re-derive target solely from `msg.recipient` / `msg.code_address` when they disagree with `executionAddress`.

**Adapter structure (composition, not merge):**

| Component | Location | Role |
| --- | --- | --- |
| `FiscoChainCallTargetAdapter` | `transaction-executor/adapters/` | Implements full `ChainExtendedPrecompileDispatch`: `classifyTarget`, `forEachStaticWarmTarget`, and `dispatch` via held `ExecutorPrecompileAdapter` (or inline dispatch delegate) |
| `ExecutorPrecompileAdapter` | `transaction-executor/adapters/` | **Evolves** from dispatch-only to being the dispatch backend inside `FiscoChainCallTargetAdapter`; does not absorb `[PRECOMPILED]` routing (that moves out of `FiscoEvmHostHooks`) |
| `OpStackChainCallTargetAdapter` | `opstack/` | Full port; holds `State*`, `l2BaseFee`, `OpStackForkSchedule`, `blockTimestamp` (today in `OpStack chain call-target adapter` ctor) |
| Null | Eth reference | `nullptr` |

**PR1 `ChainPrecompilePort` transition:** Add `eth/core/ChainExtendedPrecompileDispatch.h`. Keep `bcos/ports/ChainPrecompilePort.h` as `[[deprecated]]` alias inheriting or typedef-forwarding until PR6. `ExecutorPrecompileAdapter` continues including deprecated header in PR1–PR3; switches to `eth/core/` in PR3.

**`EvmHostHooks`:** retains `skipHostValueTransfer`, `prepareMessage`, `allowDelegateCallToPrecompile`, CREATE hooks. `tryChainPrecompile` removed from main path; deprecated shim PR4–PR6 only.

### 4. Envelope-only router (4a)

```cpp
struct PrecompileEnvelopeInput {
    state::State& state;
    bcos::evm_standard::RevisionConfig const& revision;
    execution::CallTargetDescriptor const& target;
    evmc_message const& message;
    bool skipValueTransfer;   // caller: extension != nullptr && extension->skipHostValueTransfer()
    ChainExtendedPrecompileDispatch* chainPort;  // required when kind == ChainPrecompile
};
```

**No `EvmHostHooks*` in envelope input** — policy is consumed in `resolveCallTarget` (DELEGATECALL) and by caller for `skipValueTransfer` before envelope.

Dispatch: `BuiltinPrecompile` → `EthPrecompiles::tryDispatchInCall`; `ChainPrecompile` → `chainPort->dispatch`.

### 5. `ExecutionFrame` control flow (PR4 delta only)

**Scope:** PR4 replaces only the `tryPrecompileDispatch` block. CREATE / CREATE2 nine-step pipeline (RR7) is unchanged.

**CALL / STATICCALL / DELEGATECALL (non-CREATE):**

```text
① frameTarget = resolveFrameTarget(state, revision, msg, scope)   // address work (A)
② skipVt = extension && extension->skipHostValueTransfer()
③ descriptor = resolveCallTarget(state, revision, frameTarget.routed, scope,
                                 chainPort, extension)            // RR6: BEFORE prepareNestedMessage
④ switch (descriptor.kind)
     BuiltinPrecompile / ChainPrecompile:
       return executePrecompileEnvelope({descriptor, skipValueTransfer, ...})
     EmptyAccount:
       return emptyAccountFrameResult(...)   // see §5.1 table
     PolicyRejected:
       return FrameResult{EVMC_PRECOMPILE_FAILURE, precompileHit=false}
     EvmContract:
       fall through to existing path
⑤ prepareNestedMessage (Nested only; mutates message — must follow ③)
⑥ transferFrameValue → checkpoint → runVm → finalizeFrame (unchanged)
```

**CREATE / CREATE2:** `resolveCallTarget` is not invoked for precompile switch. Existing TopLevel vs Nested ordering (RR7) unchanged. `FrameEntryOnly` warm-pin remains in address resolution step ①.

### 5.1 EmptyAccount behavior (parity with today)

| Aspect | Behavior |
| --- | --- |
| Journal | `checkpoint` → success noop → `commit` (via envelope or dedicated helper mirroring `finalizeEnvelope`) |
| `gas_left` | `message.gas` |
| `precompileHit` | `true` |
| Top-level finalize | `finalizePrecompileHit` path (`TxExecutionRunner`) — no sender nonce bump |
| Nested finalize | Same as current precompile hit |

### 6. Injection wiring

| Entry | `chainPort` construction | Lifetime | Passed to |
| --- | --- | --- | --- |
| Eth reference | `nullptr` | — | `ExecuteMessageInput.chainPort` → `FrameExecutionEnv` |
| FISCO TE | `FiscoChainCallTargetAdapter` on stack in `TransactionExecutorImpl` | per `executeViaHost` call | `FiscoExecute` / `ExecuteMessageInput` → `FrameExecutionEnv` → `EthHost::call` |
| OpStack | `OpStackChainCallTargetAdapter(state, baseFee, fork, ts)` on stack in `OpStackTxLifecycle` | per `runOpStackTxLifecycle` call | `TxPipelineContext` or parallel field → `FrameExecutionEnv` |
| Nested `EthHost::call` | **same pointer** as top-level tx | borrow | `FrameExecutionEnv.chainPort` (new field alongside `extension`) |

`FrameExecutionEnv` and `ExecuteMessageInput` gain `ChainExtendedPrecompileDispatch* chainPort{nullptr}` in PR3–PR4.

### 7. Seam discipline (extends ADR-005 Rule 1)

- `CallTargetResolver.*` and `ChainExtendedPrecompileDispatch.h` must not `#include` `bcos/` or `opstack/`.
- Chain behavior via `ChainExtendedPrecompileDispatch*` + `EvmHostHooks* extension`.
- `PrecompileActive.h` remains builtin single source.

**Execution Frame Design §2.2 amendment:** Frame-level chain customization is injected via **`ChainExtendedPrecompileDispatch*`** in addition to `EvmHostHooks* extension`. Update `2026-06-24-execution-frame-design.md` revision log in PR6.

**ADR-005 §3 table amendment (PR6):** chain precompile row moves from `EvmHostHooks::tryChainPrecompile` to `ChainExtendedPrecompileDispatch` + orchestrator injection.

### 8. Test surface (5b)

| Test module | Interface |
| --- | --- |
| `CallTargetResolverTest` | `resolveCallTarget`, `enumerateTxEntryWarmTargets` |
| `PrecompileEnvelopeTest` | `executePrecompileEnvelope` with **pre-built** descriptors (+ thin E2E smoke) |
| `CallTargetCharacterizationTest` | `runExecutionFrame` E2E; C1–C7 + **C7 depth asymmetry** |

PR2: debug dual-run should cover R1–R8 before PR4.

---

## Migration plan

| PR | Scope | Behavior change |
| --- | --- | --- |
| **PR1** | Types + `eth/core/ChainExtendedPrecompileDispatch.h`; deprecated `bcos/ports/ChainPrecompilePort` alias | None |
| **PR2** | `CallTargetResolver` + tests; optional dual-run | None |
| **PR3** | `FiscoChainCallTargetAdapter`, `OpStackChainCallTargetAdapter`; wire `chainPort` field, old path still default | None |
| **PR4** | `ExecutionFrame` delta; `executePrecompileEnvelope`; characterization baselines | **Yes** |
| **PR5** | `enumerateTxEntryWarmTargets` policy engine + adapter static tables; gas characterization | OpStack predeploy warm regression oracle (8b) |
| **PR6** | Remove shims; port `forEachClassifiedTarget`; delete migrated tests; update ADR-005, ADR-017, Execution Frame Design, `architecture-overview.md` | Cleanup |

---

## Consequences

**Positive:** Locality, warm/dispatch single engine, symmetric chain port, envelope depth preserved, interface-aligned tests.

**Costs:** Six PRs; `FrameExecutionEnv` injection surface grows; PR4 requires frozen RR6/RR7/EmptyAccount/C7 contracts.

**ADR-017:** Extended, not replaced. `bcos-evm` still zero `bcos-executor` includes.

---

## Compliance checklist

### PR4 gate (behavior switch)

- [ ] C1–C7 + **C7 depth asymmetry** baselines match `CallTargetCharacterizationTest`
- [ ] Nested **RR6:** `resolveCallTarget` before `prepareNestedMessage`
- [ ] **RR7:** CREATE TopLevel/Nested step order unchanged
- [ ] **EmptyAccount** journal / `precompileHit` / top-level finalize match §5.1
- [ ] `executeMessage` and `EthHost::call` share same `chainPort` pointer per tx

### PR5 gate (warm policy single engine)

- [x] `isTxEntryWarm(WarmPolicy)` in `CallTargetResolver.h`
- [x] OpStack adapter `{address, WarmPolicy}` static table drives `classifyTarget` + `forEachStaticWarmTarget`
- [x] FISCO adapter empty static warm enumerate; dynamic `[PRECOMPILED]` unchanged
- [x] Adapter invariant tests (`OpStackChainCallTargetAdapterTest`, `FiscoChainCallTargetAdapterTest`)
- [x] Characterization: `pr5_op_l1block_chain_static_warm_tx_entry_oracle` (state warm; chain-precompile envelope bypasses EIP-2929 access_account)
- [ ] PR6: `forEachClassifiedTarget` replaces `forEachStaticWarmTarget` on `ChainExtendedPrecompileDispatch`

### PR6 gate (full)

- [ ] `CallTargetResolver` + `ChainExtendedPrecompileDispatch` under `eth/`; no `bcos/` / `opstack/` includes in those TUs
- [ ] Builtin gates only in `PrecompileActive.h`
- [ ] `executePrecompileEnvelope` has no `isActivePrecompile` or `tryChainPrecompile`
- [x] `warmTransactionEntry` uses `enumerateTxEntryWarmTargets`
- [ ] FISCO `[PRECOMPILED]` only in `FiscoChainCallTargetAdapter`
- [ ] OpStack L1/GasOracle only in `OpStackChainCallTargetAdapter`
- [ ] `CallTargetResolverTest` covers R1–R8, W1–W2; PR2 dual-run if enabled
- [ ] TE smoke: `FiscoChainCallTargetAdapter` full port
- [ ] `EvmHostHooks::tryChainPrecompile` removed from main path
- [ ] FISCO: `OpStackTxLifecycle`, `FiscoExecute`, `EthHost.cpp` wired
- [x] **PR5:** OpStack predeploy warm has gas-test evidence (`CallTargetCharacterizationTest` PR5 gate)
- [ ] ADR-005 §3, ADR-017, Execution Frame Design §2.2, `architecture-overview.md` (ADR 001–024) updated

---

## References

- Grilling 2026-06-26: D1–D5.
- Subagent doc review 2026-06-26: v1.1 patches.
- `docs/superpowers/specs/2026-06-26-call-target-resolver-design.md` — normative spec.
- ADR-017, ADR-018, `PrecompileRouterCharacterizationTest.cpp`.
