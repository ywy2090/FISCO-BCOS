# EVM Execution Trace — Design Spec

**Date:** 2026-07-06  
**Status:** Draft — revised after multi-agent review (2026-07-06)  
**Scope:** Unified execution trace for EEST failure diagnosis, production observability, and future `debug_traceTransaction` / `debug_transaction` RPC reuse  
**Architecture choice:** Approach C — hybrid (`evmone::Tracer` for VM-inner layers + bcos hooks for blind spots, unified `TraceCollector`)

**Related specs:**

- `docs/superpowers/specs/2026-07-03-eest-blockchain-test-runner-parity-design.md` — `--trace` CLI (P2 there; CLI implemented in Phase 3 here). **Note:** that spec explicitly avoids new JSON deps; this spec follows it (see §9.1).
- `docs/superpowers/specs/2026-07-02-evmone-inspired-eth-gtest-design.md` — Eth GTest fixtures share `applyEthMessage` path
- `eth/trace/EvmTrace.h` — existing traceId + `EVM_LOG` context (retained; see §5.5 scope boundary)
- `eth/trace/EvmOpcodeProbe.h` — existing opcode histogram probe (migrated to `EvmOpcodeHistogramTracer` via TracerRegistry in **Phase 2**)

**Frozen decisions (brainstorming + review):**

| Item | Choice |
|------|--------|
| Architecture | **Approach C** — evmone Tracer + bcos Frame/Host hooks + unified Collector |
| Zero overhead when off | **TraceGate** + thread_local effective policy; **Tier A** default (≤1% branch cost), **Tier B** compile strip (zero instructions) |
| Production default binary | `EVM_EXECUTION_TRACE=ON` (Tier A) — hook sites present, runtime-gated |
| Production release preset | `EVM_EXECUTION_TRACE=OFF` (Tier B) — strict zero-instruction |
| P0 scope | EEST failure diagnosis + production error auto-capture (CallTree) + gas/nonce/value ledger (§6.4–6.5). **No** RPC / replay / structLogger in P0 |
| debug_transaction reuse | **Phase 2** — TraceService + TracerRegistry + structLogger + JSON exporter |
| JSON library | **boost::property_tree** (repo convention; no new nlohmann dependency) |
| Test diff layer | `test/eth-eest-test/debug/` — EestFailureReporter, StateDiffReport, GasAuditReport (test binary only) |

---

## 1. Problem Statement

### 1.1 Current state

bcos-evm has fragmented, test-oriented debugging facilities:

| Facility | Location | Env / trigger | Gap |
|----------|----------|---------------|-----|
| `EvmTraceScope` + `EVM_LOG` | `eth/trace/EvmTrace.h` | Always active at apply entry | Not structured; no call tree |
| `EEST_PROBE` | `test/.../EthMessageAdapter.cpp` | `EEST_PROBE=1` | Test-only; no expected/actual diff |
| `EvmOpcodeProbe` | `eth/trace/EvmOpcodeProbe.*` | `EEST_OPCODE_TRACE=1` | Histogram only; attach-once bug (§5.4) |
| `WarmAccessProbe` | `eth/state/WarmAccessProbe.h` | `EEST_WARM_PROBE=1` | EIP-2929 counters only |

EEST state-full debugging exposed recurring pain:

1. FAIL logs lack `variantKey` / `casePath` — slow to locate failing cases across 4000+ tests.
2. `stateRoot mismatch` reports got/want hex only — no per-account or storage slot diff.
3. Gas deviations (e.g. eip6780 Δ=19900) require manual coinbase/sender balance arithmetic.
4. CALLCODE / DELEGATECALL + SELFDESTRUCT paths need call-tree correlation; opcode probes alone are insufficient.
5. No production path to inspect EVM execution after an anomaly, and no foundation for `debug_transaction` RPC.

### 1.1a Non-negotiable principles

These three drive every design decision below; each links to the section that guarantees it.

| # | Principle | Guaranteed by |
|---|-----------|---------------|
| **P1** | **Switchable, no overhead when off.** A master switch controls trace. When off, no evmone tracer is attached and no work is done. Consensus release builds set `EVM_EXECUTION_TRACE=OFF` for strict **zero-instruction** (Tier B); the default binary keeps hook sites at Tier A (≤1% branch cost, runtime-gated). | §5 (Tiers A/B), §5.2 (TraceGate), §16 (verifiable criteria) |
| **P2** | **Reusable by debug_transaction.** The trace engine lives in `eth/trace/` (no test dependency) and executes through the same `applyEthMessage` path. Phase 2 `TraceService::replay()` reuses it verbatim — no separate debug VM. | §3.3, §9 (TraceService, TracerRegistry, buildEthReplayRequest) |
| **P3** | **Clear, attributable records of every consensus-relevant state change.** Not just gas/nonce/value: also storage, code, empty-account clearing, warm/cold, refund, transient storage, delegation, selfdestruct, and journal revert — each with before/after + reason + frame id, so "what went wrong" is read off the trace, not reconstructed by arithmetic. | §6.2 (HostEvent), §6.4 (GasLedger), §6.5 (AccountStateDelta), §6.6 (divergence taxonomy) |

### 1.2 Goals

1. **EEST (P0):** On FAIL, emit variantKey, gas audit, account/storage diff, and a rerun hint within one log block.
2. **Production (P0):** Master switch off by default; on error, capture call tree + first failure frame with Tier-A cost when off (Tier B when release-stripped).
3. **debug_transaction reuse (Phase 2):** RPC layer calls `TraceService::replay()` using the same `applyEthMessage` path and TracerRegistry — no duplicate debug VM.
4. **State-change ledger (P0):** Per-tx `GasLedger` + per-account `AccountStateDelta` + frame-correlated `HostEvent`s covering gas/nonce/value **and** the high-probability divergence sources of §6.6 (empty-account clear, warm/cold, refund, transient, CREATE collision, selfdestruct, and journal checkpoint/revert).
5. **Zero-overhead contract:** See §5 for the precise scope boundary and the Tier A / Tier B distinction.

### 1.3 Out of scope (initial delivery, P0)

- RPC `debug_traceTransaction` / `debug_transaction` and `TraceService` (→ Phase 2)
- `structLogger`, `TracerRegistry`, JSON exporter (→ Phase 2)
- `prestateTracer`, JS tracers (→ Phase 3 or later)
- Always-on StructLog on consensus nodes (never)
- OPStack / Fisco separate trace formats (shared core; `route` field distinguishes)
- Historical archive-state infrastructure (node/storage layer; bcos-evm defines error codes + injection interface only)
- Replacing BoostLog / existing `EVM_LOG(DEBUG)` lines

---

## 2. Approaches Considered

### Approach A — evmone Tracer only

Implement call tree and StructLog solely via `evmone::Tracer`.

**Pros:** Minimal code; opcode detail from evmone.  
**Cons:** Blind to precompile fast-path, `transferOrFail`, Host `selfdestruct` / `set_storage`, and pre-EVM reject paths. **Insufficient.**

### Approach B — bcos instrumentation only

Hand-roll call tree at frame / Host layer without evmone Tracer.

**Pros:** Full bcos path coverage.  
**Cons:** Duplicates StructLog; drifts from evmone on opcode gas attribution. **Insufficient for StructLog.**

### Approach C — Hybrid + unified Collector (chosen)

`evmone::Tracer` for VM-inner frames and optional StructLog; bcos hooks for fast-path and Host events; all events flow into one `TraceCollector` with pluggable sinks.

**Pros:** Complete coverage; test and production share core; evmone upgrades stay localized.  
**Cons:** Two instrumentation layers to maintain; requires per-tx Tracer attach/detach discipline.

**Decision: Approach C.**

---

## 3. Architecture

### 3.1 Layer diagram

```text
┌──────────────────────────────────────────────────────────────────┐
│  Consumers                                                        │
│  EEST runner (FAIL reporter) │ RPC debug_transaction (Phase 2) │  │
│                              │ Node log                          │
└───────────────┬──────────────────────┬───────────────────────────┘
                │                      │
                ▼                      ▼
┌───────────────────────────┐  ┌──────────────────────────────────┐
│ test/eth-eest-test/debug/ │  │ eth/trace/TraceService (Phase 2) │
│ EestFailureReporter       │  │ replay() / getCachedTrace()      │
│ StateDiffReport           │  └───────────────┬──────────────────┘
│ GasAuditReport            │                  │
└───────────────┬───────────┘                  │
                └──────────────┬────────────────┘
                               ▼
                ┌──────────────────────────────┐
                │ ExecutionTraceSession        │
                │ TraceGate + effective policy │
                └──────────────┬───────────────┘
                               │
           ┌───────────────────┴───────────────────┐
           ▼                                       ▼
┌─────────────────────────┐           ┌─────────────────────────┐
│ evmone Tracer layer      │           │ bcos hook layer          │
│ EvmCallTreeTracer        │           │ Frame free-functions     │
│ EvmStructLogTracer (P2)  │           │ EthHost callbacks        │
│ EvmOpcodeHistogramTracer │           │ stateTransition phases   │
│ TracerRegistry (P2)      │           │ HostEventCollector       │
└──────────────┬──────────┘           └──────────────┬──────────┘
               │                                     │
               └─────────────────┬───────────────────┘
                                 ▼
                ┌──────────────────────────────┐
                │ TraceCollector (aggregates    │
                │ one or more ITraceSink)       │
                └──────────────┬───────────────┘
                               │
       ┌───────────────┬───────┴────────┬────────────────┐
       ▼               ▼                ▼                ▼
  LogTraceSink   RingBufferSink   JsonTraceSink    (test) sink
                                    (Phase 2)      via reporter
```

### 3.2 Directory layout

```text
bcos-evm/eth/trace/
  TraceGate.h                 # effective-policy accessor + thread_local collector
  TracePolicy.h / .cpp        # TracePolicy, TracePolicyOverride (RAII), current(), loadFromIni()
  TraceLimits.h                 # size caps
  ExecutionTraceSession.h/.cpp
  TraceCollector.h            # aggregates ITraceSink; typed onCallFrame/onHostEvent/onSummary
  ITraceSink.h                  # LogTraceSink, RingBufferSink base
  LogTraceSink.h / .cpp
  TraceRingBuffer.h / .cpp      # backing store for RingBufferSink
  RingBufferSink.h / .cpp
  EvmCallTreeTracer.h / .cpp    # evmone::Tracer → call frames
  EvmOpcodeHistogramTracer.h/.cpp  # replaces EvmOpcodeProbe (Phase 2 rename; Phase 1 keeps probe)
  HostEventCollector.h / .cpp   # Frame/Host hook events → CallFrameEvent/HostEvent
  TraceTypes.h                  # TraceLevel, CallFrameEvent, HostEvent, ExecutionSummary
  EvmTrace.h                    # existing (retained; see §5.5)

  # ---- Phase 2 (debug_transaction) ----
  TracerRegistry.h / .cpp       # name → IExecutionTracer factory
  IExecutionTracer.h
  EvmStructLogTracer.h / .cpp
  TraceJsonExporter.h / .cpp    # boost::property_tree → JSON string
  TraceService.h / .cpp         # replay + cache API for RPC

bcos-evm/test/eth-eest-test/debug/
  EestFailureReporter.h / .cpp
  StateDiffReport.h / .cpp
  GasAuditReport.h / .cpp
```

### 3.3 Dependency rules

- `eth/trace/` links `bcos-evm-eth` only — **no** dependency on `eth-eest-test`.
- `TraceCollector` exposes an `ITraceSink` registration API; test-only sinks are registered at link time from `test/eth-eest-test/debug/`. No test sink lives in `eth/trace/`.
- RPC / executor modules depend on `TraceService.h` (Phase 2) and inject `TraceReplayDependencies`.

---

## 4. Trace Levels and Tracer Registry

### 4.1 TraceLevel

```cpp
enum class TraceLevel : uint8_t {
    Off        = 0,   // no session, no attach
    Summary    = 1,   // apply/stateTransition tx in-out only
    CallTree   = 2,   // + evmone execution_start/end + Frame fast-path hooks
    HostEvents = 3,   // + EthHost sstore/selfdestruct/transfer/log
    StructLog  = 4,   // + evmone instruction_start (Phase 2)
};
```

Level → activation map:

| Level | evmone tracer attached | bcos Frame hooks | Host hooks | Sinks populated |
|-------|------------------------|------------------|------------|-----------------|
| Off | none | no | no | none |
| Summary | none | no | no | ExecutionSummary + GasLedger |
| CallTree | EvmCallTreeTracer | yes | no | + CallFrameEvent |
| HostEvents | EvmCallTreeTracer | yes | yes | + HostEvent (sstore/balance/nonce/selfdestruct) + AccountStateDelta |
| StructLog | + EvmStructLogTracer | yes | yes | + struct entries |

Note: `GasLedger` (§6.4) is always populated once a session is active (even Summary), since it is a read-through of settlement fields, not per-op work. `HostEvent` balance/nonce records require Level ≥ HostEvents, and are also force-armed by `capture_on_error` so a failed tx always has full gas/nonce/value attribution.

### 4.2 Tracer registry (Phase 2, RPC-facing names)

| Tracer name | Level equivalent | Output |
|-------------|------------------|--------|
| `callTracer` | CallTree (+ HostEvents for selfdestruct) | Nested calls (geth subset, §9.4) |
| `structLogger` | StructLog | struct entries (geth subset) |
| `histogram` | — | opcode gas histogram (test / CLI) |
| `prestateTracer` | — | Phase 3+ |

```cpp
// Phase 2
std::unique_ptr<IExecutionTracer> TracerRegistry::create(
    std::string_view name, TraceCollector& collector, TraceLimits const& limits);
```

For non-RPC callers, `TracePolicy::level` selects tracers directly (see §4.1). `TracePolicy` also carries `captureOnErrorTracer` (default `callTracer` semantics = CallTree + selfdestruct HostEvents).

---

## 5. Zero-Overhead Contract

### 5.1 Scope boundary (read first)

The zero-overhead contract covers **`ExecutionTraceSession`, the `EVM_TRACE` hook sites, and the evmone Tracer attach path only**. It does **not** cover the pre-existing `EvmTraceScope` / `EVM_LOG` machinery, which already runs on every tx at apply entry (`ApplyEthMessage.cpp:62`, `ApplyOpStackMessage.cpp:67`, `ApplyFiscoMessage.cpp:99`) and costs one atomic `fetch_add` plus a thread_local vector push/pop per tx regardless of trace. Whether to gate `EvmTraceScope` behind the master switch is deferred to §15 OQ#2.

### 5.2 Master switch and effective policy

```ini
[evm.trace]
enabled=false              # consensus default
level=0
capture_on_error=false     # requires enabled=true OR the dedicated error path (§9.3)
sample_rate=0
allowlist=                  # txHash list, evaluated per-tx (NOT startup-cached)
max_frames=256
max_host_events=4096
max_structlog_entries=0
max_ring_summaries=1000
```

Environment override (development / EEST): `EVM_TRACE_ENABLED=1 EVM_TRACE_LEVEL=2 ...`.

**Effective policy model (resolves review Blocker — static cache vs per-request override):**

TraceGate does **not** cache a single process-wide `enabled` bool. It reads a thread-local effective policy so that consensus, EEST, and RPC replay can differ per thread and per tx:

```cpp
class TraceGate {
public:
    // Reads thread_local effective policy (override stack top, else global default).
    static bool enabled() noexcept;
    static TraceCollector* collector() noexcept;   // thread_local; nullptr when inactive
    static TracePolicy const& effective() noexcept;
private:
    // global default loaded from ini/env (reloadable); thread_local override stack on top.
};

// RAII: installs {policy, collector, tracers} for one execution; pops on destruction.
class TracePolicyOverride {
public:
    TracePolicyOverride(TracePolicy policy, TraceCollector* collector) noexcept;
    ~TracePolicyOverride() noexcept;   // restores previous thread_local state
};
```

**Per-tx activation** (evaluated at `ExecutionTraceSession` construction, not at startup):

```text
effective.enabled =
    override.active                     // RPC replay / EEST session
 || global.allowlist.contains(txHash)   // dynamic, per-tx
 || global.captureOnError               // arms error-capture path
 || sample(global.sample_rate)
 || (global.enabled && global.level > Off)
```

**Invariant:** `EVM_TRACE` (which checks `collector()`) and the evmone attach path (which checks `enabled()`) MUST be armed together. `ExecutionTraceSession` sets the thread_local collector and effective policy atomically at construction, so both layers agree.

### 5.3 EVM_TRACE hook macro

```cpp
#if EVM_EXECUTION_TRACE     // Tier A (default)
  #define EVM_TRACE(event, ...) \
      do { \
          if (auto* _c = ::bcos::evm::trace::TraceGate::collector(); _c != nullptr) \
              _c->event(__VA_ARGS__); \
      } while (0)
#else                        // Tier B (release strip)
  #define EVM_TRACE(event, ...) ((void)0)
#endif
```

`event` is a typed method on `TraceCollector` (e.g. `onCallFrameEnter(CallFrameEvent const&)`), not a generic sink call.

### 5.4 Two overhead tiers

| Tier | Build | Hook-site cost when disabled | Guarantee |
|------|-------|------------------------------|-----------|
| **Tier A** | `EVM_EXECUTION_TRACE=ON` (default dev/prod binary) | one thread_local pointer load + predictable branch per hook site | ≤1% p99 regression vs baseline (§16) |
| **Tier B** | `EVM_EXECUTION_TRACE=OFF` (release hardening preset) | zero instructions (macro expands empty) | strict zero-instruction |

```cmake
option(EVM_EXECUTION_TRACE "Enable EVM trace hook sites" ON)
# release/production hardening preset sets EVM_EXECUTION_TRACE=OFF
```

Hook sites are at **frame / host boundaries** (§7.2), i.e. cost is `O(frames + host ops)`, not `O(opcodes)`. When disabled, no evmone tracer is attached, so the interpreter loop is unchanged. First access of the thread_local pointer on a worker thread may incur a one-time TLS init (thread-scoped, not per-tx).

### 5.5 Per-transaction evmone Tracer lifecycle

**Fixes the existing attach-once bug.** `EvmOpcodeProbe::attachIfNeeded` returns early if a tracer is already present (`EvmOpcodeProbe.cpp:87-90`) and the `thread_local` VM is never detached, so a probe outlives the tx that armed it and accumulates across txs (verified against `VMInstance.cpp:27-31`, `InnerExecute.cpp:278-281`).

```cpp
// InnerExecute depth==0 entry — ENTIRE block skipped when TraceGate::enabled() is false
// (when disabled, InnerExecute is identical to today; no remove_tracers call).
if (TraceGate::enabled())
{
    auto* evmoneVm = static_cast<evmone::VM*>(input.vm->get_raw_pointer());
    evmoneVm->remove_tracers();                       // evmone 0.21 API (verified)
    for (auto& tracer : session.evmoneTracers())
        evmoneVm->add_tracer(std::move(tracer));
    // ... execute ...
    session.flush();
    evmoneVm->remove_tracers();                       // REQUIRED — no cross-tx leakage
}
```

The primary VM to instrument is `input.vm` (the `evmc::VM*` injected via `StateTransitionContext`), which `runCallFrame` → `runVm` actually executes on — **not** the `VMInstance` thread_local VM.

---

## 6. Core Types

### 6.1 CallFrameEvent

```cpp
struct CallFrameEvent {
    uint32_t frameId;
    uint32_t parentFrameId;
    int depth;
    evmc_call_kind kind;             // CALL/DELEGATECALL/CALLCODE/CREATE/CREATE2 (+ STATICCALL via flags)
    evmc_address from;
    evmc_address to;                 // for CREATE/CREATE2: created address (geth callTracer parity)
    evmc_address codeAddress;
    int64_t gasIn;
    int64_t gasOut;
    int64_t gasUsed;
    evmc_status_code status;
    std::string_view exitStep;       // "runVm" | "fastPath" | "transferOrFail" | ...
    bool fromEvmone;                 // true = evmone Tracer; false = bcos hook
    bcos::bytes input;               // debug_transaction / callTracer
    bcos::bytes output;              // populated on exit
    evmc_uint256be value;
    std::optional<evmc_address> createdAddress;
    std::optional<std::string> revertReason;   // decoded Solidity revert, if any
};
```

`STATICCALL` is distinguished via `EVMC_STATIC` flag; the trace enum mapping (and existing `callKind()` in `EvmTrace.h`, which currently lacks `EVMC_STATICCALL`) must be extended in Phase 1.

#### 6.1.1 Frame single-source rule (normative — prevents double-counting)

Because a VM-executed frame is visible to **both** the `evmone::Tracer` and the bcos frame layer, the two instrumentation layers MUST partition responsibility so each frame produces **exactly one** `CallFrameEvent`:

- **VM-executed frames** (any frame that reaches `runVm` → `evmone` interpreter): emitted **only** by `EvmCallTreeTracer` (`fromEvmone = true`). bcos code MUST NOT emit a `CallFrameEvent` for these.
- **Short-circuit / fast-path frames** (frames that complete *before* `runVm`): emitted **only** by the bcos hooks at the `runCallTargetFastPath` and `transferOrFail` early returns (`fromEvmone = false`). evmone never sees these, so there is no overlap.

**`frameId` allocation has a single source:** `TraceCollector::nextFrameId()` (monotonic per session). Both layers draw ids from it, so ids are globally unique within a tx. **Parent linkage:** `EvmCallTreeTracer` maintains the depth stack and sets `parentFrameId` for VM frames. Precise `parentFrameId` for bcos leaf frames (linking to the enclosing evmone frame) is deferred to **Phase 1**; Phase 0 sets `parentFrameId = 0` for bcos leaf frames and documents this in the plan. A trace consumer MUST treat the frame set as a forest keyed by `frameId`/`parentFrameId`, not assume contiguous ids.

### 6.2 HostEvent

```cpp
enum class HostEventKind : uint8_t {
    // --- account field mutations ---
    Sload, Sstore, BalanceChange, NonceChange, CodeChange,
    // --- account lifecycle (EIP-161 state clearing) ---
    AccountTouched,      // touch → candidate for empty-account deletion
    AccountDeleted,      // pruned (empty-account clear) or selfdestruct removal
    CreateContract,      // new account installed (CREATE/CREATE2)
    CreateCollision,     // CREATE aborted: target has nonce!=0 or code
    Selfdestruct,        // SELFDESTRUCT (EIP-6780 wasCreatedInTx)
    // --- EIP-specific / non-persistent-but-consensus-relevant ---
    AccessWarmth,        // EIP-2929 cold→warm decision (drives gas → balance)
    RefundChange,        // refund counter add/sub (drives gasUsed → balance)
    TransientStorage,    // EIP-1153 TSTORE (must clear at tx end)
    Delegation7702,      // EIP-7702 delegation designator write
    PrecompileCall, Log,
    // --- journal boundary (revert-correctness) ---
    Checkpoint,          // journal slice opened (frame enter)
    Commit,              // slice merged into parent
    Revert,              // slice rolled back (records what was undone)
};

enum class NonceChangeReason : uint8_t {
    TxSenderBump, CreateBump, CreatedInit /* EIP-161 nonce=1 */, Auth7702,
};

enum class BalanceChangeReason : uint8_t {
    GasBuy, GasRefund, FeeToCoinbase, CallValue, Selfdestruct, Create,
};

struct HostEvent {
    HostEventKind kind;
    int depth;
    uint32_t frameId;                // correlate with CallFrameEvent
    evmc_address address;
    // Sload/Sstore:      evmc_bytes32 slot, valueOld, valueNew
    // BalanceChange:     evmc_uint256be before, after; BalanceChangeReason reason; opt counterparty
    // NonceChange:       uint64_t before, after; NonceChangeReason reason
    // CodeChange:        evmc_bytes32 codeHashBefore, codeHashAfter; size_t sizeBefore, sizeAfter
    // AccountTouched:    bool becameEmpty
    // AccountDeleted:    enum { EmptyClear, Selfdestruct } cause
    // CreateContract:    evmc_address created; bool addressFromCreate2
    // CreateCollision:   evmc_address target; enum { NonceSet, CodePresent } cause
    // Selfdestruct:      evmc_address beneficiary; bool wasCreatedInTx
    // AccessWarmth:      opt evmc_bytes32 slot; bool wasCold           (address-level if slot empty)
    // RefundChange:      int64_t delta, after; std::string_view reason ("sstore"/"selfdestruct"/"clear")
    // TransientStorage:  evmc_bytes32 slot, value
    // Delegation7702:    evmc_address authority, delegate
    // PrecompileCall:    evmc_address precompile; int64_t gasCost
    // Log:               uint8_t numTopics; size_t dataSize
    // Checkpoint/Commit/Revert: uint32_t journalSizeBefore; uint32_t undoneMutations (Revert)
    HostEventPayload payload;        // std::variant of the above
};
```

**Every balance and nonce mutation emits a `HostEvent` with before/after and a reason.** This is the backbone of Principle 3: gas debits/refunds, coinbase fee, call value, CREATE endowment, and all nonce bumps are individually recorded and frame-correlated, so a divergence (e.g. eip6780 coinbase Δ=59700) is attributable to a specific event rather than reconstructed by arithmetic.

The lifecycle / EIP / journal kinds cover the **high-probability stateRoot-mismatch sources beyond gas/nonce/value** (§6.6): empty-account clearing, CREATE collision, warm/cold gas drift, refund accounting, transient-storage clearing, 7702 delegation, and — most subtle — **journal revert correctness** (a `Revert` event reports how many mutations were undone, so a frame that leaves dirty warm-set / created-set / storage entries behind is directly visible).

### 6.3 ExecutionSummary

```cpp
struct ExecutionSummary {
    evmc_status_code status;
    StateTransitionExitKind exitKind;
    int64_t gasLimit;
    int64_t gasUsed;
    int64_t gasRefund;
    bool reachedEvmEntry;
    std::optional<evmc_bytes32> stateRoot;
    uint32_t callFrameCount;
    uint32_t maxDepth;
    std::optional<uint32_t> firstFailureFrameId;
    GasLedger gas;                    // §6.4 — full gas lifecycle
    std::string diagnosis;            // §6.5 — human-readable "what went wrong"
    bool truncated;
};
```

### 6.4 GasLedger — gas lifecycle (Principle 3)

Per-tx gas accounting broken into the exact stages the state-transition pipeline applies, so any gas discrepancy points to one stage rather than a single opaque `gasUsed`:

```cpp
struct GasLedger {
    int64_t gasLimit;
    int64_t intrinsicGas;         // DeductIntrinsicGas (calldata + access list + auth tuples)
    int64_t floorDataGas;         // EIP-7623 floor (if applied)
    int64_t gasAtEvmEntry;        // gas handed to innerExecute
    int64_t evmGasLeft;           // evmc_result.gas_left
    int64_t evmGasUsed;           // gasAtEvmEntry - evmGasLeft
    int64_t evmGasRefund;         // host refund counter
    int64_t refundApplied;        // min(refund, used/quotient) per EIP-3529
    int64_t gasUsedFinal;         // billed to sender / receipt
    // fee accounting (wei):
    evmc_uint256be gasPrice;
    evmc_uint256be effectiveTip;  // priority fee to coinbase
    evmc_uint256be feeToCoinbase;
    evmc_uint256be feeBurned;     // base fee burn (EIP-1559), if modeled
};
```

The GasLedger is populated from the existing `gasSettlementSnapshot` / `StateTransitionContext::gasAccounting` fields (already captured at `StateTransitionExecute.cpp` `captureSettlementSnapshot`), so no new computation is introduced on the hot path — the values are read-through only when a session is active.

### 6.5 AccountStateDelta — pre/post per account (Principle 3)

Aggregated view derived from `HostEvent`s (and the final StateDiff), used by both the EEST diff and `prestateTracer`:

```cpp
struct AccountStateDelta {
    evmc_address address;
    enum class Kind { Created, Deleted, Modified } kind;
    std::optional<FieldDelta<uint64_t>>       nonce;    // before/after
    std::optional<FieldDelta<evmc_uint256be>> balance;
    std::optional<FieldDelta<evmc_bytes32>>   codeHash;
    std::optional<FieldDelta<size_t>>         codeSize;
    std::vector<StorageSlotDelta>             storage;  // changed slots only
    bool emptyAfter;                                     // EIP-161: should be pruned
    bool selfDestructed;
    std::optional<evmc_address>               delegate;  // EIP-7702 designator
};

template <typename T> struct FieldDelta { T before; T after; };
```

**Diagnosis chain:** `GasLedger` (which stage) → `HostEvent` (which mutation + reason + frame) → `AccountStateDelta` (net effect per account) → `CallFrameEvent.firstFailureFrame` (where in the call tree). These four together answer "gas/nonce/value 在哪一步、因何变更、导致哪个账户与预期不符".

### 6.6 State-divergence taxonomy (what the trace must attribute)

Beyond gas/nonce/value, these are the empirically high-probability stateRoot / receiptsRoot / logsHash mismatch sources (from EEST 6780 + warm/cold debugging). Each maps to a `HostEventKind` and an instrumentation point (§7.2):

| Rank | Divergence source | Why it diverges | HostEvent | Root affected |
|------|-------------------|-----------------|-----------|---------------|
| 1 | **Storage slot** | value; zero→delete vs set-0; original/dirty snapshot | `Sstore` | stateRoot |
| 1 | **Empty-account clearing (EIP-161)** | touched empty account pruned vs kept | `AccountTouched`/`AccountDeleted` | stateRoot |
| 1 | **code / codeHash** | deposit-gas fail, empty codeHash vs no account, EIP-3541/170/3860 | `CodeChange` | stateRoot |
| 1 | **SELFDESTRUCT / 6780** | same-tx-created delete decision; beneficiary/wipe timing | `Selfdestruct`/`AccountDeleted` | stateRoot |
| 1 | **CREATE collision / address** | target nonce!=0 or code → abort; created nonce=1 | `CreateContract`/`CreateCollision` | stateRoot |
| 2 | **refund counter (EIP-3529)** | cap quotient, SSTORE/SELFDESTRUCT refund → gasUsed → balances | `RefundChange` + GasLedger | stateRoot (via balance) |
| 2 | **warm/cold (EIP-2929/2930)** | cold↔warm misjudge → gas → balances (the 6780 Δ19900) | `AccessWarmth` | stateRoot (via balance) |
| 3 | **EIP-7702 delegation** | designator code, authority nonce, delegated storage ownership | `Delegation7702` | stateRoot |
| 3 | **transient storage (EIP-1153)** | must clear at tx end; leak pollutes next tx | `TransientStorage` | (cross-tx) |
| 3 | **block-level system contracts** | EIP-4788/2935/7002/7251 write state at block edges | (block runner) | stateRoot |
| 3 | **withdrawals (EIP-4895)** | block-level balance credit | `BalanceChange` | withdrawalsRoot |
| 4 | **logs / receipt / bloom** | topic/data/status/cumulativeGas | `Log` | logsHash / receiptsRoot |
| 5 | **journal revert correctness** | partial revert leaves dirty storage / warm-set / created-set / refund | `Checkpoint`/`Commit`/`Revert` | stateRoot (indirect, hardest) |

Rank-5 is the most destructive and least field-local: the bug is not a single mutation but a **rollback boundary** — a REVERT/OOG frame that fails to undo (or wrongly undoes) warm-set, created-set, or refund entries. The `Revert` event's `undoneMutations` count + the surrounding `Checkpoint` make this observable per frame.

---

## 7. Instrumentation Points

### 7.1 evmone Tracer (`EvmCallTreeTracer`, `EvmStructLogTracer`)

Verified against `EvmOpcodeProbe` and evmone 0.21 `tracing.hpp`:

| Hook | Signature | Data |
|------|-----------|------|
| `on_execution_start` | `(evmc_revision, const evmc_message&, evmc::bytes_view code)` | push frame: depth, kind, addresses, gasIn, code size |
| `on_instruction_start` | `(uint32_t pc, const intx::uint256* stack_top, int stack_height, int64_t gas, const evmone::ExecutionState&)` | StructLog only; opcode = `state.original_code[pc]` |
| `on_execution_end` | `(const evmc_result&)` | gasOut, status, create_address, pop frame |

### 7.2 bcos hooks (evmone blind spots)

**Note:** the frame functions are free functions in namespace `bcos::evm::execution` (not `EvmCallFrame::` members); the only public entry is `runCallFrame`.

| Location | Symbol (verified) | Event |
|----------|-------------------|-------|
| Frame fast-path | `execution::runCallTargetFastPath` (`EvmCallFrame.cpp:144`) | precompile / empty-account short-circuit frame |
| Value transfer | `execution::transferOrFail` (`EvmCallFrame.cpp:230`) | CallFrame value + `BalanceChange`(CallValue); `INSUFFICIENT_BALANCE` early return |
| Frame steps enter/exit | `execution::runFrameSteps` (`EvmCallFrame.cpp:496`) | frame metadata + exitStep |
| Host selfdestruct | `EthHost::selfdestruct` (`EthHost.cpp:178`) | SELFDESTRUCT + `wasCreatedInTx` (EIP-6780) + `BalanceChange`(Selfdestruct) |
| Host storage | `EthHost::set_storage` (`EthHost.cpp:80`) | SSTORE (Level 3+) |
| Host log | `EthHost::emit_log` (`EthHost.cpp:305`) | LOG (Level 3+, optional) |
| **Sender nonce bump** | tx entry (`ApplyEthMessage` / prepareTxEntry) | `NonceChange`(TxSenderBump) |
| **CREATE nonce bump** | `execution::` CREATE pre-checkpoint (`EvmCallFrame.cpp`) | `NonceChange`(CreateBump / CreatedInit) |
| **7702 auth nonce** | `Eip7702` apply (`eth/eip/Eip7702.cpp`) | `NonceChange`(Auth7702) |
| **buyGas debit** | `applyEthMessage` buyGas (`ApplyEthMessage.cpp:95-112`) | `BalanceChange`(GasBuy) + GasLedger stage |
| **gas refund / coinbase fee** | fee settlement (`eth/settlement/EthFeeSettlement.cpp`) | `BalanceChange`(GasRefund / FeeToCoinbase) + GasLedger |
| **journal checkpoint/commit/revert** | `State::checkpoint` (`State.cpp:173`), `State::commit` (`:178`), `State::revert` (`:200`) | `Checkpoint` / `Commit` / `Revert`(+undoneMutations) — Rank-5 revert correctness |
| **empty-account touch / clear** | touched-set (`State.cpp:288-294`), `finalize_self_destructs` (`State.hpp:82`) | `AccountTouched`(becameEmpty) / `AccountDeleted` (EIP-161) |
| **warm/cold access** | `State::warm_up_address` / `warm_up_storage` (`State.hpp:69-70`) | `AccessWarmth`(wasCold) — EIP-2929 |
| **refund counter** | `State::add_refund` / `sub_refund` / `clear_refund` (`State.cpp:552/567`) | `RefundChange`(delta, reason) |
| **transient storage** | `State::set_transient_storage` / `clearAllTransientStorage` (`State.hpp:62/67`) | `TransientStorage` — EIP-1153 |
| **CREATE deploy / collision** | `State::touchCreateDeploymentAccount` (`State.hpp:85`), `CreateDeployment.h` | `CreateContract` / `CreateCollision` / `CodeChange` |
| Pre-EVM reject | `stateTransitionExecute` phases (`StateTransitionExecute.cpp:54-192`) | Summary: rules / gas-affordable / intrinsic reject |

All bcos hooks use the `EVM_TRACE(...)` macro only. Balance/nonce hooks (Level 3, or always when `capture_on_error` fires) are what make Principle 3 attributable: each mutation carries before/after + reason + frameId.

### 7.3 Session entry points (Current → Target)

| Entry | Today | Target |
|-------|-------|--------|
| `applyEthMessage` | `EvmTraceScope` only | + `ExecutionTraceSession` (Phase 0) |
| `applyOpStackMessage` | `EvmTraceScope` only | + `ExecutionTraceSession` (Phase 2) |
| `applyFiscoMessage` | `EvmTraceScope` only | + `ExecutionTraceSession` (Phase 2) |

`InnerExecute` also creates a kernel-route `EvmTraceScope` at depth 0 when no traceId is active (`InnerExecute.cpp:265-268`); the new Session must not double-create.

Session reads `TraceGate::effective()` unless a `TracePolicyOverride` is active (RPC replay / EEST).

---

## 8. Output Sinks

`TraceCollector` aggregates one or more `ITraceSink`. Naming is fixed as follows (class name = file name):

| Sink | Consumer | When active |
|------|----------|-------------|
| `LogTraceSink` | Production ops / grep | Summary + CallTree on error |
| `RingBufferSink` (backed by `TraceRingBuffer`) | Post-mortem / `getCachedTrace()` | Recent N summaries + full tree on error |
| `JsonTraceSink` (Phase 2) | RPC response | replay / explicit request |
| `EestFailureReporter` | EEST stderr on FAIL | test binary only (registered from test/) |

### 8.1 Structured log schema (production)

```text
[EVM] trace=<id> txHash=0x... route=eth event=call_exit frame=3 depth=2
      kind=CALLCODE status=REVERT gasUsed=19900 exitStep=runVm
```

### 8.2 EEST FAIL block (target)

```text
FAIL eth.eest.cancun.state.eip6780
  variant=tests/cancun/eip6780_selfdestruct/...::CALLCODE-CREATE2
  casePath=fixtures/state_tests/cancun/eip6780_selfdestruct/...
  fork=Cancun  assert=stateRoot
  got=0x77a3903a...  want=0xbe1a920e...
  gasLedger: limit=300000 intrinsic=21000 evmUsed=247469 refund=0 final=268469 (want 288369, Δ=-19900)
  hypothesis: gas metering (coinbase Δ=59700 = 19900×10 ✓)
  firstFailureFrame: depth=2 kind=CALLCODE frame=3
  hostEvents:
    NonceChange sender before=0 after=1 (TxSenderBump)
    BalanceChange sender -3000000 (GasBuy) frame=0
    BalanceChange coinbase +2684690 (FeeToCoinbase) frame=0   [want +2883690]
    Selfdestruct 0x71762a63 beneficiary=0x... wasCreatedInTx=true frame=3
  accountDelta: 0x71762a63 storage slot[0x02] want=0x... got=<missing>
  rerun: EVM_TRACE_ENABLED=1 --manifest .../eth-eest-6780-callcode-probe.json
```

The block realizes the diagnosis chain (§6.5): GasLedger localizes the stage, the `BalanceChange`/`NonceChange` events attribute each wei/nonce delta to a frame + reason, and `accountDelta` shows the net divergence — no manual arithmetic.

---

## 9. debug_transaction Reuse Contract (Phase 2)

### 9.1 TraceService (public API for RPC)

JSON is emitted via `boost::property_tree` (repo convention; no nlohmann dependency).

```cpp
namespace bcos::evm::trace {

// Everything the replay needs to reconstruct a consensus-equivalent request.
struct TxMeta {
    bcos::bytesConstRef txRlpOrData;   // or protocol::Transaction handle
    int64_t blockNumber;
    uint32_t txIndex;
    // block context needed to match consensus (see §9.5 injection):
    state::BlockInfo blockInfo;        // baseFee, blobBaseFee, prevRandao, coinbase, timestamp
    bcos::evm::RevisionConfig revisionConfig;
    bool isCall{false};                // debug_traceTransaction → false (buyGas + settlement)
};

struct TraceOptions {
    std::string tracer{"callTracer"};
    bool onlyTopCall{false};
    bool withLog{false};
    uint32_t timeoutMs{0};             // 0 = node-enforced
    uint32_t reexec{0};
};

enum class TraceError { Ok, TxNotFound, StateNotAvailable, Timeout, TracerUnknown, TraceDisabled };

struct TraceReplayRequest {
    bcos::h256 txHash;
    int64_t blockNumber;
    TraceOptions options;
};

struct TraceReplayResult {
    TraceError error;
    std::string errorMessage;
    boost::property_tree::ptree trace;   // serialized by caller
    std::string source;                  // "replay" | "ring_buffer"
    bool truncated{false};
};

// Node injects lookup + request assembly; bcos-evm never reads the DB directly.
struct TraceReplayDependencies {
    std::function<std::optional<TxMeta>(bcos::h256)> findTx;
    std::function<std::optional<state::StateView>(int64_t blockNumber)> stateAt;
    std::function<state::BlockHashes(int64_t blockNumber)> blockHashesAt;
    // Assembles a consensus-equivalent EthMessageRequest from TxMeta + state.
    // RECOMMENDED: node calls the shared transaction-executor helper
    // buildEthReplayRequest(TxMeta, StateView, BlockHashes) rather than re-implementing.
    std::function<EthMessageRequest(TxMeta const&, state::StateView const&,
                                    state::BlockHashes const&)> buildRequest;
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};
};

class TraceService {
public:
    static TraceService& instance();
    std::optional<boost::property_tree::ptree> getCachedTrace(bcos::h256 txHash) const;
    task::Task<TraceReplayResult> replay(TraceReplayRequest, TraceReplayDependencies) const;
};

}  // namespace bcos::evm::trace
```

**`buildRequest` must populate** (any omission diverges from consensus): `message`, `blockInfo` (baseFee/blobBaseFee/prevRandao/coinbase/timestamp), `blockHashes`, `revisionConfig`, fee caps, `accessList`, 7702 `authorizations`, `blobVersionedHashes` (type-0x03), `txNonce`, `txValue`, and `isCall=false`. A shared `buildEthReplayRequest` in transaction-executor (wrapping the existing `newEVMCMessage` + `fillTransactionGasFields` + `fillWeb3Fields` + EIP-4844 blob decode) is the reference implementation; the Eth web3 field path must gain type-0x03 blob decoding (today only OpStack decodes it).

### 9.2 Replay flow

```text
RPC debug_traceTransaction(txHash, { tracer: "callTracer" })
  → TraceService::replay(request, deps)
      → deps.findTx(txHash)                       // → TxMeta (blockNumber, txIndex, blockInfo)
      → deps.stateAt(blockNumber - 1)             // node responsibility (archive)
      → deps.blockHashesAt(blockNumber)
      → deps.buildRequest(meta, state, hashes)    // isCall=false; full field set
      → TracePolicyOverride{ enabled=true, tracer=request.tracer }  // scoped, thread_local
      → ExecutionTraceSession + TracerRegistry
      → applyEthMessage(request)                  // identical path to block execution
      → TraceJsonExporter → ptree
  → return to RPC client
```

### 9.3 Three execution modes (shared Collector)

| Mode | Trigger | Policy | Sink |
|------|---------|--------|------|
| Live error capture | Consensus `apply*` | `capture_on_error` arms CallTree | RingBuffer + Log |
| EEST debug | Test FAIL / `--trace` | `TracePolicyOverride` (env/CLI) | EestFailureReporter |
| debug_transaction | RPC replay (Phase 2) | `TracePolicyOverride` (enabled=true) | JsonTraceSink |

`capture_on_error` is a lightweight armed path: when off globally, the session is created lazily only after a non-SUCCESS/`exitKind != Completed` is observed, replaying the frame summary already collected — it does not require full `enabled=true` for normal txs.

### 9.4 geth callTracer JSON compatibility (Phase 2 subset)

Per-frame fields:

```text
type, from, to (created address for CREATE/CREATE2), gas, gasUsed,
input, output, value, error, revertReason, calls[]
```

Options: `onlyTopCall` (P0 of Phase 2), `withLog` (deferred, metadata `withLog:false`).

Response metadata:

- `truncated: true` when `max_frames` exceeded
- `fastPathsCaptured: true` when bcos hooks are active (i.e. precompile/short-circuit frames are present). *(Renamed from the earlier confusing `missingFastPaths`.)*
- Root-frame `gasUsed` is overwritten with `ExecutionSummary::gasUsed` (receipt-equivalent), matching geth `OnTxEnd`.

### 9.5 Node-layer responsibilities (out of bcos-evm scope)

- Archive / historical state at block N-1 (`stateAt`)
- Block header / hashes / ledger config (`blockHashesAt`, `TxMeta.blockInfo`)
- Tx lookup by hash (`findTx`)
- RPC method registration, auth (debug namespace typically restricted), timeout enforcement

bcos-evm returns `TraceError::StateNotAvailable` when `stateAt` fails — no silent fallback. The `TraceService` implementation MUST NOT include `bcos-ledger` / DB headers.

### 9.6 getCachedTrace vs replay

| | getCachedTrace | replay |
|--|----------------|--------|
| Source | RingBuffer (live execution) | re-execution at N-1 |
| Completeness | may be `truncated`; input/output truncated to 4 bytes + len (§14) | full bytes; tracer-selectable |
| Use | fast post-mortem of a recently-executed / errored tx | explicit RPC request |

**Rules:** RPC defaults to `replay`. `getCachedTrace` returns only when present; results carry `source:"ring_buffer"` + `truncated`. When archive state is unavailable for replay, return cached (if any) with a warning, else `StateNotAvailable`. RingBuffer entries share `TraceLimits`; oldest summaries drop first.

---

## 10. TracePolicy

```cpp
struct TracePolicy {
    bool enabled{false};
    TraceLevel level{TraceLevel::Off};
    bool captureOnError{false};
    TraceLevel errorLevel{TraceLevel::CallTree};
    std::string captureOnErrorTracer{"callTracer"};
    double sampleRate{0.0};
    std::vector<bcos::h256> allowlist;
    TraceLimits limits;

    static TracePolicy const& current() noexcept;   // thread_local effective (override or global)
    static TracePolicy loadFromIni(/* config */);
};

struct TraceLimits {
    size_t maxFrames{256};
    size_t maxHostEvents{4096};
    size_t maxStructLogEntries{0};
    size_t maxRingSummaries{1000};
};
```

**Activation priority (highest first):** `TracePolicyOverride (RPC/EEST)` > `allowlist(txHash)` > `enabled && level>Off (explicit/env)` > `captureOnError` > `sampleRate` > `Off`. All evaluated per-tx at session construction; nothing is startup-cached.

---

## 11. Migration from Existing Probes

| Legacy | Replacement | Phase |
|--------|-------------|-------|
| `EEST_PROBE=1` | `EVM_TRACE_ENABLED=1` + `EestFailureReporter` | Phase 1 |
| `EEST_OPCODE_TRACE=1` | `EVM_TRACE_ENABLED=1` + histogram (via `EvmOpcodeHistogramTracer`) | Phase 2 |
| `EEST_WARM_PROBE=1` | separate for now; fold into HostEvents | Phase 2+ |
| `EvmOpcodeProbe::attachIfNeeded` | removed; per-tx attach/detach (§5.5) | Phase 1 removes call site; Phase 2 renames class |

Legacy env vars emit a deprecation warning for one release, then are removed.

---

## 12. Implementation Phases

### Phase 0 — Foundation (P0)

1. `TraceGate` (thread_local effective policy), `TracePolicy` + `TracePolicyOverride`, `TraceLimits`, `TraceTypes` (incl. `TraceLevel`), `TraceCollector` + `ITraceSink`
2. `EvmCallTreeTracer` + per-tx attach/detach on the executing VM (fixes attach-once **on the trace-session path**). The per-tx attach/detach is owned by `ExecutionTraceSession` (RAII), which the Phase 0 plan constructs at `applyEthMessage` over the same `input.vm` that `runVm` executes on — equivalent to the §5.5 `InnerExecute` sketch, without a second session. **Removing the legacy `EvmOpcodeProbe` attach call sites is Phase 1** (see §11) — Phase 0 leaves them in place; the probe's attach-once guard no-ops when the session already attached a tracer.
3. bcos Frame hooks: `runCallTargetFastPath`, `transferOrFail` early returns (leaf frames evmone never sees). Note: normal `runVm` frames are emitted by `EvmCallTreeTracer` only — bcos hooks must **not** re-emit them (see §6.1 de-dup rule), so `runFrameSteps` is the *location* of the two early-return hooks, not a full-frame hook.
4. `ExecutionTraceSession` at `applyEthMessage` (Eth first) + Summary events at stateTransition/buyGas reject
5. `TraceGateDisabledTest` (no attach when off) + `TraceCallTreeTest` (nested CALL tree)

### Phase 1 — EEST + production error capture + gas/nonce/value ledger (P0)

1. `GasLedger` (read-through from `gasSettlementSnapshot` / `gasAccounting`) + `AccountStateDelta`
2. `HostEvent` balance/nonce hooks (buyGas, refund, coinbase fee, sender/CREATE/7702 nonce) — Principle 3
3. `HostEvent` state-divergence hooks (§6.6): `State.cpp` checkpoint/commit/revert, empty-account touch/clear, warm/cold, refund, transient, CREATE collision
4. `EestFailureReporter` + `StateDiffReport` + `GasAuditReport` (consume GasLedger + HostEvents)
5. FAIL output: variantKey, casePath, GasLedger, hostEvents, accountDelta, journal revert summary, rerun hint
6. `LogTraceSink` structured events + `RingBufferSink` / `TraceRingBuffer`
7. `capture_on_error` armed path (§9.3)
8. Extend `callKind()` / enum mapping for `STATICCALL`

### Phase 2 — debug_transaction + StructLog (deferred from P0)

1. `TracerRegistry` + `IExecutionTracer`; rename `EvmOpcodeProbe` → `EvmOpcodeHistogramTracer`
2. `EvmStructLogTracer` with `max_structlog_entries`
3. `TraceJsonExporter` (boost::property_tree) — callTracer + structLogger subsets
4. `TraceService` + `TraceReplayDependencies`; shared `buildEthReplayRequest` (incl. Eth blob decode)
5. Extend Session to `applyOpStackMessage`, `applyFiscoMessage`; Host hooks (selfdestruct, set_storage)

### Phase 3 — CLI + extended tracers (P2)

1. EEST runner `--trace` / `--trace-summary` / `--trace-tracer=callTracer`
2. `prestateTracer` plugin
3. `EVM_EXECUTION_TRACE=OFF` release preset (Tier B) + benchmark harness (§16)

---

## 13. Testing Strategy

| Test | Location | Validates | Phase |
|------|----------|-----------|-------|
| `TraceGateDisabledTest` | `test/eth/` | No Tracer on VM when off | 0 |
| `TraceCallTreeTest` | `test/eth/` | Nested CALL/CREATE2 tree | 0 |
| `TraceFastPathTest` | `test/eth/` | Precompile frame without evmone | 0 |
| `TracePolicyOverrideTest` | `test/eth/` | Override enables one tx, restores after | 0 |
| `GasLedgerTest` | `test/eth/` | intrinsic/evm/refund/coinbase stages match settlement | 1 |
| `NonceValueEventTest` | `test/eth/` | sender/CREATE/7702 nonce + buyGas/refund/fee balance events with before/after + reason | 1 |
| `JournalRevertTraceTest` | `test/eth/` | Checkpoint/Revert events; undoneMutations matches; no dirty warm/created leak (pairs with `StateJournalRevertTest`) | 1 |
| `StateDivergenceEventTest` | `test/eth/` | empty-account clear, warm/cold, refund, transient, CREATE collision emit correct HostEvents (§6.6) | 1 |
| `EestFailureReporterTest` | `test/eth-eest-test/` | GasLedger + hostEvents + accountDelta formatting | 1 |
| `RingBufferCaptureTest` | `test/eth/` | error capture stored + retrievable | 1 |
| `TraceSelfdestructHostTest` | `test/eth/` | HostEvent on EIP-6780 path | 2 |
| `TraceServiceReplayTest` | `test/eth/` | in-memory deps → replay → ptree | 2 |

---

## 14. Performance and Security

- **Tier A hot path:** one `TraceGate::collector()` thread_local load + predictable branch per hook site when disabled; cost is `O(frames + host ops)`, not `O(opcodes)`. First worker-thread access may pay a one-time TLS init.
- **Baseline caveat:** the current baseline already includes `EvmTraceScope` (atomic + TLS vector per tx, §5.1); §16 benchmarks compare against that same baseline. New hooks add only Tier-A branch cost on top.
- **Hard caps:** enforce `max_frames`, `max_host_events`, `max_structlog_entries`, `max_ring_summaries`; set `ExecutionSummary::truncated`.
- **Production data:** default log truncates `input`/`output` to 4 bytes + length; full bytes only in replay mode.
- **StructLog:** never written to default node log; JSON response or debug file only.
- **RPC auth:** debug namespace gated at node config (documented; not implemented here).

---

## 15. Open Questions

1. **P0 priority fork:** EEST-first vs production-error-first ordering within Phase 0/1 (architecture unchanged either way).
2. **EvmTraceScope gating:** fold traceId allocation under `TraceGate::enabled() || logLevel>=DEBUG` in Phase 1, or leave always-on? (Affects whether the §16 baseline is re-taken.)
3. **RingBuffer sizing:** `max_ring_summaries=1000` default — tune from ops feedback.

---

## 16. Success Criteria

1. **Tier A overhead (functional + perf):**
   - `TraceGateDisabledTest`: with `enabled=false`, `input.vm` has no tracer attached after execution (functional).
   - Perf: `EVM_EXECUTION_TRACE=ON`, `enabled=false`, a fixed benchmark workload (documented fixture + block size), N≥1000 iterations, **p99 ≤ baseline + 1%** at the **same commit**, baseline defined as HEAD-before-hooks and **including** existing `EvmTraceScope`.
2. **Tier B overhead:** `EVM_EXECUTION_TRACE=OFF` produces zero instructions at hook sites (verified by symbol/objdump diff vs `ON`).
3. **EEST diagnosis:** eip6780 probe manifest FAIL prints variantKey, gas Δ, and first failure frame without manual log scraping.
4. **No cross-tx leakage:** no tracer remains attached on `input.vm` after a tx completes.
5. **State-change attribution (P3):** for the eip6780 probe, the trace records every nonce bump, balance change (with reason/frame), warm/cold decision, refund change, and journal revert (undoneMutations), and `GasLedger.gasUsedFinal` equals the receipt gas — the §6.6 divergence sources are attributable without manual arithmetic (verified by `GasLedgerTest`, `NonceValueEventTest`, `JournalRevertTraceTest`, `StateDivergenceEventTest`).
6. **(Phase 2) Replay:** `TraceService::replay()` with in-memory fixture deps produces a valid `callTracer` ptree through `applyEthMessage` (`TraceServiceReplayTest`).

---

## 17. Deferred spec-revision checklist (from 2026-07-06 multi-agent review)

Phase 0 (§12) is now internally consistent for implementation (probe-removal → Phase 1 per §11; frame single-source rule §6.1.1). The following review findings are **substantive but non-blocking for Phase 0**; resolve them when the corresponding Phase 1/2 work is planned. Each is a spec-level TODO, not yet reflected in the body above.

**Phase 1 (before EEST/HostEvents/GasLedger implementation):**

- **`capture_on_error` lazy-create vs replay (review B1):** §9.3 says "lazy-create session after non-SUCCESS and replay already-collected frames", but nothing is collected before activation. Pick one and rewrite §9.3 + §5.2: (A) arm a lightweight CallTree session from tx entry when `capture_on_error=true` (define its Tier-A cost bound), or (B) re-execute the tx on error. Remove the contradictory clause.
- **Level matrix vs error/tracer exceptions (review B5/M2):** §4.1 matrix says CallTree ⇒ "Host hooks: no", but the note + §4.2 `callTracer` force selfdestruct/balance HostEvents; §5.2 never defines `effective.level`. Introduce `effectiveLevel = max(level, errorLevel)` (or a capability bitmap) and make §5.2/§10 a single priority cascade emitting both `effective.enabled` and `effective.level`.
- **EthHost hooks phase vs P3 success criteria (review B4):** §16.5 / §8.2 (eip6780 `Selfdestruct`/`BalanceChange`/`accountDelta`) require `EthHost::selfdestruct`/`set_storage`, but §12 defers Host hooks to Phase 2 item 5. Move the eip6780/4844-relevant Host hooks into Phase 1, or lower §16.5/§8.2 Phase-1 reachability claims.
- **SSTORE EIP-2200/3529 fields (review M7):** §6.2 `Sstore` payload needs `original` + `current` + `evmc_storage_status` (available from `EthHost::set_storage`), else refund/gas divergence is not attributable. Link `RefundChange` to `frameId+slot`.
- **tx-entry warm not instrumented (review M9):** §7.2 hooks `warm_up_address`/`warm_up_storage` but tx-entry prewarm uses `warm_up_*_no_journal` (`PrepareState.h`); add a `prepareState` / no-journal hook, tag `AccessWarmth` with `TxEntryWarm`.
- **code-deposit failure hook (review M8):** add `applyCreateCodeDepositGas` / `installCreatedContractCode` instrumentation (`EvmCallFrame.cpp` finalize) for EIP-3541/170/3860 `CodeChange`/collision attribution.
- **§6.6 ↔ §7.2 coverage gaps (review M10):** block-level 4788/2935/7002/7251, withdrawals (4895), `Sload` hook, coinbase-warm — either add hooks or explicitly mark "out of tx-trace scope / block-trace Phase N".
- **`HostEventCollector` phase (review M12):** listed in §3.2/§3.1 but absent from any phase's deliverable list — add a Phase 1 skeleton.
- **`EVM_TRACE` macro + `EVM_EXECUTION_TRACE` option:** §5.3 macro is the normative hook form; the Phase 0 plan uses direct `if (collector())` guards and defers the macro + CMake option (Tier B) to Phase 3. Decide whether Host hooks (Phase 1) adopt the macro to avoid two call styles.
- **EvmOpcodeProbe two attach sites (review M4/N1):** §5.5 root-cause cites `VMInstance.cpp`, but the consensus attach is `InnerExecute.cpp` → `input.vm`; a second site exists in `VMInstance.cpp`. Phase 1 removes **both** and §16.4 should assert "`m_vm` has no residual tracer across txs". Correct the §5.5 reference.
- **P1 zero-overhead wording (review M1) + §16 baseline (M3):** freeze §15 OQ#2 (gate `EvmTraceScope`?) before re-taking the §16 baseline; specify the A/B dual-build benchmark method (same commit, `EVM_EXECUTION_TRACE` on/off, `enabled=false`, documented workload/threads/warmup).

**Phase 2 (before debug_transaction):**

- **Eth 4844 replay decode (review M5):** `buildEthReplayRequest` must call `decodeEip4844BlobFields` (today only OpStack decodes 0x03) and populate `blobVersionedHashes`/`blobGasFeeCap`/fee caps.
- **§9.5 vs §9.6 fallback (review M6):** reconcile "no silent fallback" vs "return cached + warning" — define `error != Ok` + `partial:true`/`source` fields, or forbid fallback.
- **per-tx detach exception safety + threading (review M11):** `ExecutionTraceSession` RAII already guarantees detach on any exit; §9 must add the replay threading constraint (execute on the tx thread / exclusive executor queue; thread_local override re-entrancy).
- **`HostEventPayload` variant schema (review N3):** freeze the `std::variant` member set (add §6.2.1) to avoid reporter/exporter churn.
