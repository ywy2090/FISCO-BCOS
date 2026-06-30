# CallTargetResolver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge frame address resolution + precompile/chain target classification into `CallTargetResolver`, unify chain adapters behind `ChainCallTargetPort`, and thin `PrecompileRouter` to envelope-only — without changing CREATE pipeline (RR7) or EVM gas math.

**Architecture:** Six incremental PRs (spec §6). PR1–PR3 add types/adapters with zero behavior change. PR4 switches `ExecutionFrame` precompile path. PR5 updates tx-entry warm via `enumerateTxEntryWarmTargets`. PR6 removes deprecated shims and old tests.

**Tech Stack:** C++17+, CMake 3.28, Boost.Test, evmone, FISCO-BCOS `bcos-evm` (eth/bcos/opstack static libs), `transaction-executor` adapters.

**Spec / ADR:** `docs/superpowers/specs/2026-06-26-call-target-resolver-design.md` v1.1 · `bcos-evm/docs/adr/024-call-target-resolver-deepening.md` v1.1

## Global Constraints

- `eth/execution/CallTargetResolver.*` and `eth/ports/ChainCallTargetPort.h` **must not** `#include` `bcos/` or `opstack/` headers.
- Builtin precompile gates **only** in `PrecompileActive.h`; resolver calls, never duplicates.
- `executePrecompileEnvelope` **must not** call `isActivePrecompile`, `tryChainPrecompile`, or `classifyTarget`.
- Chain-over-builtin precedence preserved; C7: `classifyTarget` only when `emptyCode || scope == Nested`.
- Nested RR6: `resolveCallTarget` **before** `prepareNestedMessage`.
- CREATE RR7 nine-step pipeline **unchanged** in PR4.
- PR5 OpStack predeploy warm merges **only after** geth reference or C2 gas baseline recorded; otherwise plumbing + no new warm behavior.
- `bcos-evm` source **zero** `bcos-executor` includes (ADR-017).
- New `eth/*.cpp` / `opstack/*.cpp` picked up automatically by `file(GLOB_RECURSE)` in `bcos-evm/CMakeLists.txt`.

## File Structure Map

| File | Responsibility |
| --- | --- |
| `bcos-evm/eth/ports/ChainCallTargetPort.h` | Neutral port: classify + dispatch + static warm enumerate |
| `bcos-evm/eth/execution/CallTargetResolver.h/.cpp` | Types, `resolveCallTarget`, `enumerateTxEntryWarmTargets`; absorbs classification from `PrecompileRouter.cpp` + address work from `FrameTargetResolver.cpp` |
| `bcos-evm/bcos/ports/ChainPrecompilePort.h` | PR1–PR5: deprecated alias; PR6: delete |
| `bcos-evm/eth/precompiled/PrecompileRouter.h/.cpp` | PR4+: `executePrecompileEnvelope`; envelope only |
| `bcos-evm/eth/execution/EvmCallFrame.h/.cpp` | PR4: PR4 delta + `FrameContext::chainPort` |
| `bcos-evm/eth/execution/InnerExecute.h` | PR3–4: `ExecuteMessageInput::chainPort` |
| `bcos-evm/eth/state/EthHost.cpp` | PR4: pass `chainPort` into nested `FrameContext` |
| `bcos-evm/opstack/OpStackChainCallTargetAdapter.h/.cpp` | OpStack port impl |
| `transaction-executor/.../FiscoChainCallTargetAdapter.h/.cpp` | FISCO classify + dispatch composition |
| `bcos-evm/test/bcos/adapters/InMemoryChainCallTargetAdapter.h` | Test double for full port |
| `bcos-evm/test/eth/CallTargetResolverTest.cpp` | R1–R8, W1–W2 |
| `bcos-evm/test/eth/PrecompileEnvelopeTest.cpp` | Envelope with pre-built descriptors |
| `bcos-evm/test/cross/CallTargetCharacterizationTest.cpp` | C1–C7 E2E (replaces PrecompileRouterCharacterizationTest) |

---

## PR1 — Types + Port Interface (no behavior change)

### Task 1: ChainCallTargetPort + CallTargetResolver types

**Files:**
- Create: `bcos-evm/eth/ports/ChainCallTargetPort.h`
- Create: `bcos-evm/eth/execution/CallTargetResolver.h`
- Modify: `bcos-evm/bcos/ports/ChainPrecompilePort.h`

**Interfaces:**
- Produces: `CallTargetKind`, `WarmPolicy`, `CallTargetDescriptor`, `ChainCallTargetPort` vtable

- [ ] **Step 1: Create `ChainCallTargetPort.h`**

```cpp
#pragma once

#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm {

struct ChainCallTargetPort {
    virtual ~ChainCallTargetPort() = default;

    virtual std::optional<execution::CallTargetDescriptor> classifyTarget(
        state::State& state,
        evmc_address const& executionAddress,
        evmc_message const& msg,
        execution::FrameScope scope) = 0;

    virtual std::optional<evmc_result> dispatch(
        evmc_revision rev, evmc_message const& msg) = 0;

    virtual void forEachStaticWarmTarget(
        std::function<void(evmc_address const&)> const& consume) const = 0;
};

}  // namespace bcos::evm
```

Note: use `std::function` instead of `std::invocable` template in vtable for ABI simplicity.

- [ ] **Step 2: Create `CallTargetResolver.h`**

```cpp
#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/VmHostPolicy.h"
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm { struct ChainCallTargetPort; }

namespace bcos::evm::execution {

enum class CallTargetKind {
    EvmContract,
    BuiltinPrecompile,
    ChainPrecompile,
    EmptyAccount,
    PolicyRejected,
};

enum class WarmPolicy { Never, TxEntryAlways, TxEntryIfStatic, FrameEntryOnly };

struct CallTargetDescriptor {
    CallTargetKind kind{CallTargetKind::EvmContract};
    evmc_address dispatchAddress{};
    WarmPolicy warmPolicy{WarmPolicy::Never};
    evmc_message routed{};
};

CallTargetDescriptor resolveCallTarget(
    state::State& state,
    bcos::evm_standard::RevisionConfig const& revision,
    evmc_message msg,
    FrameScope scope,
    ChainCallTargetPort* chainPort,
    state::VmHostPolicy* extension);

void enumerateTxEntryWarmTargets(
    bcos::evm_standard::RevisionConfig const& cfg,
    ChainCallTargetPort const* chainPort,
    std::function<void(evmc_address const&)> const& consume);

}  // namespace bcos::evm::execution
```

- [ ] **Step 3: Create stub `CallTargetResolver.cpp`**

```cpp
#include "bcos-evm/eth/execution/CallTargetResolver.h"

namespace bcos::evm::execution {

CallTargetDescriptor resolveCallTarget(
    state::State&, bcos::evm_standard::RevisionConfig const&, evmc_message msg,
    FrameScope, ChainCallTargetPort*, state::VmHostPolicy*)
{
    return CallTargetDescriptor{.kind = CallTargetKind::EvmContract, .routed = msg};
}

void enumerateTxEntryWarmTargets(
    bcos::evm_standard::RevisionConfig const&, ChainCallTargetPort const*,
    std::function<void(evmc_address const&)> const&)
{}

}  // namespace bcos::evm::execution
```

- [ ] **Step 4: Deprecate `ChainPrecompilePort`**

Replace `bcos-evm/bcos/ports/ChainPrecompilePort.h` contents:

```cpp
#pragma once

#include "bcos-evm/eth/ports/ChainCallTargetPort.h"
#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm {

[[deprecated("Use ChainCallTargetPort (ADR-024)")]]
struct ChainPrecompilePort : ChainCallTargetPort {
    ~ChainPrecompilePort() override = default;
};

}  // namespace bcos::evm
```

**Important:** `ExecutorPrecompileAdapter` still inherits `ChainPrecompilePort` until PR3; add default `classifyTarget` / `forEachStaticWarmTarget` overrides in PR3 adapter work. For PR1, keep old `ChainPrecompilePort` as **separate** struct with only `dispatch` if inheritance breaks TE — preferred PR1 approach: **do not inherit yet**; only add new headers and leave `ChainPrecompilePort.h` unchanged except a comment pointing to ADR-024. Minimal PR1:

```cpp
// bcos/ports/ChainPrecompilePort.h — add at top:
// ADR-024: superseded by eth/ports/ChainCallTargetPort.h (PR6 removal).
```

And create new files only. Revisit alias in PR3 when adapters implement full port.

- [ ] **Step 5: Build verify**

Run: `cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op`
Expected: SUCCESS (GLOB picks up `CallTargetResolver.cpp`)

- [ ] **Step 6: Commit (optional checkpoint)**

```bash
git add bcos-evm/eth/ports/ChainCallTargetPort.h \
        bcos-evm/eth/execution/CallTargetResolver.h \
        bcos-evm/eth/execution/CallTargetResolver.cpp
git commit -m "feat(bcos-evm): ADR-024 PR1 CallTargetResolver types and ChainCallTargetPort"
```

---

## PR2 — CallTargetResolver implementation + tests (no main-path switch)

### Task 2: InMemoryChainCallTargetAdapter test double

**Files:**
- Create: `bcos-evm/test/bcos/adapters/InMemoryChainCallTargetAdapter.h`

**Interfaces:**
- Produces: `InMemoryChainCallTargetAdapter` implementing full `ChainCallTargetPort`

- [ ] **Step 1: Write adapter header**

```cpp
#pragma once

#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/ports/ChainCallTargetPort.h"
#include <functional>
#include <optional>
#include <vector>

namespace bcos::evm::test {

class InMemoryChainCallTargetAdapter final : public ChainCallTargetPort
{
public:
    using ClassifyFn = std::function<std::optional<execution::CallTargetDescriptor>(
        state::State&, evmc_address const&, evmc_message const&, execution::FrameScope)>;
    using DispatchFn = std::function<std::optional<evmc_result>(evmc_revision, evmc_message const&)>;

    explicit InMemoryChainCallTargetAdapter(ClassifyFn classify, DispatchFn dispatch = {})
      : m_classify(std::move(classify)), m_dispatch(std::move(dispatch))
    {}

    void addStaticWarmTarget(evmc_address const& addr) { m_staticWarm.push_back(addr); }

    std::optional<execution::CallTargetDescriptor> classifyTarget(
        state::State& s, evmc_address const& a, evmc_message const& m,
        execution::FrameScope scope) override
    {
        return m_classify ? m_classify(s, a, m, scope) : std::nullopt;
    }

    std::optional<evmc_result> dispatch(evmc_revision r, evmc_message const& m) override
    {
        return m_dispatch ? m_dispatch(r, m) : std::nullopt;
    }

    void forEachStaticWarmTarget(std::function<void(evmc_address const&)> const& c) const override
    {
        for (auto const& a : m_staticWarm) { c(a); }
    }

private:
    ClassifyFn m_classify;
    DispatchFn m_dispatch;
    std::vector<evmc_address> m_staticWarm;
};

}  // namespace bcos::evm::test
```

---

### Task 3: Implement resolveCallTarget (migrate logic from PrecompileRouter)

**Files:**
- Modify: `bcos-evm/eth/execution/CallTargetResolver.cpp`
- Reference: `bcos-evm/eth/precompiled/PrecompileRouter.cpp` (lines 59–157)
- Reference: `bcos-evm/eth/execution/FrameTargetResolver.cpp`

**Interfaces:**
- Consumes: `resolveFrameTarget`, `PrecompileActive.h`, `Eip7702.h`, `ChainCallTargetPort`
- Produces: `resolveCallTarget` matching spec §4.5

- [ ] **Step 1: Write failing test R1 (builtin precompile)**

Create `bcos-evm/test/eth/CallTargetResolverTest.cpp`:

```cpp
#define BOOST_TEST_MODULE CallTargetResolverTest

#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/execution/FrameTargetResolver.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test {
namespace {

evmc_address precompileAddr(uint8_t low)
{
    evmc_address a{};
    a.bytes[19] = low;
    return a;
}

}  // namespace

BOOST_AUTO_TEST_CASE(R1_empty_code_active_builtin)
{
    bcos::evm::test::InMemoryEvmStateReader base;
    state::State state{base};
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_PRAGUE;

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = precompileAddr(0x04);
    msg.code_address = msg.recipient;
    msg.gas = 100000;

    auto frame = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::TopLevel);
    auto desc = execution::resolveCallTarget(
        state, cfg, frame.routed, execution::FrameScope::TopLevel, nullptr, nullptr);

    BOOST_CHECK(desc.kind == execution::CallTargetKind::BuiltinPrecompile);
    BOOST_CHECK(desc.warmPolicy == execution::WarmPolicy::TxEntryAlways);
}
}  // namespace bcos::evm::test
```

Add to `bcos-evm/test/cmake/EthTests.cmake`:

```cmake
add_executable(CallTargetResolverTest eth/CallTargetResolverTest.cpp)
target_include_directories(CallTargetResolverTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(CallTargetResolverTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME CallTargetResolver COMMAND CallTargetResolverTest)
```

- [ ] **Step 2: Run test — expect FAIL**

Run: `cmake --build build --target CallTargetResolverTest && ctest -R CallTargetResolver -V`
Expected: FAIL (`BuiltinPrecompile` not returned)

- [ ] **Step 3: Implement `resolveCallTarget`**

Port classification from `PrecompileRouter.cpp`:
1. Compute `executionAddress` from `msg` (caller passes `frame.routed`; use `resolveFrameTarget` output's execution address via `resolveExecutionAddress` helper — extract shared helper or call `resolveFrameTarget` internally).
2. Read code at target; detect 7702 designator → `EvmContract`.
3. DELEGATECALL + `!extension->allowDelegateCallToPrecompile()` + empty active builtin → `PolicyRejected`.
4. If `(emptyCode || scope == Nested) && chainPort`: `classifyTarget(state, executionAddress, msg, scope)`.
5. Else if empty + `isActivePrecompile` → `BuiltinPrecompile`.
6. Else if empty → `EmptyAccount`.
7. Else → `EvmContract`.

Include: `PrecompileRouter.cpp` private helpers (`is7702DelegationDesignator`, `isActiveEmptyPrecompileTarget`) — **move** to `CallTargetResolver.cpp` anonymous namespace (do not duplicate long-term).

- [ ] **Step 4: Add tests R2–R8, W1–W2 per spec §7.4**

Migrate cases from:
- `test/eth/PrecompileRouterPrecedenceTest.cpp` → R4
- `test/eth/PrecompileRouter7702Test.cpp` → R5 (classification parts only)
- `test/bcos/FiscoVmHostPolicyTest.cpp` → R7 (via `InMemoryChainCallTargetAdapter`)

- [ ] **Step 5: Implement `enumerateTxEntryWarmTargets`**

```cpp
void enumerateTxEntryWarmTargets(
    bcos::evm_standard::RevisionConfig const& cfg,
    ChainCallTargetPort const* chainPort,
    std::function<void(evmc_address const&)> const& consume)
{
    precompiled::forEachActivePrecompile(cfg, [&](evmc_address const& a) { consume(a); });
    if (chainPort != nullptr)
    {
        chainPort->forEachStaticWarmTarget(consume);
    }
}
```

Test W1/W2 in same file.

- [ ] **Step 6: Run full resolver tests**

Run: `ctest -R CallTargetResolver -V`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add bcos-evm/eth/execution/CallTargetResolver.cpp \
        bcos-evm/test/eth/CallTargetResolverTest.cpp \
        bcos-evm/test/cmake/EthTests.cmake \
        bcos-evm/test/bcos/adapters/InMemoryChainCallTargetAdapter.h
git commit -m "feat(bcos-evm): ADR-024 PR2 CallTargetResolver implementation and tests"
```

---

## PR3 — Chain adapters + chainPort wiring (main path still legacy)

### Task 4: OpStackChainCallTargetAdapter

**Files:**
- Create: `bcos-evm/opstack/OpStackChainCallTargetAdapter.h`
- Create: `bcos-evm/opstack/OpStackChainCallTargetAdapter.cpp`
- Reference: `bcos-evm/opstack/OpStackVmHostPolicy.h`

**Interfaces:**
- Produces: `OpStackChainCallTargetAdapter` with ctor `(state::State*, bcos::u256 l2BaseFee, OpStackForkSchedule, uint64_t blockTimestamp)`

- [ ] **Step 1: Move dispatch + classify from `OpStackVmHostPolicy::tryChainPrecompile` into adapter**
- [ ] **Step 2: `forEachStaticWarmTarget` emits `OP_L1_BLOCK_PREDEPLOY`, `OP_GAS_PRICE_ORACLE_PREDEPLOY`**
- [ ] **Step 3: Unit test in `test/opstack/OpStackChainCallTargetAdapterTest.cpp` (new) — R8 classify + W2 enumerate**
- [ ] **Step 4: Build `bcos-evm-op` — SUCCESS**

---

### Task 5: FiscoChainCallTargetAdapter (TE)

**Files:**
- Create: `transaction-executor/bcos-transaction-executor/adapters/FiscoChainCallTargetAdapter.h`
- Modify: `transaction-executor/bcos-transaction-executor/adapters/ExecutorPrecompileAdapter.h` (include path → `eth/ports/ChainCallTargetPort.h` optional PR3)
- Reference: `bcos-evm/bcos/FiscoVmHostPolicy.cpp` (`tryChainPrecompile`, `parseDynamicPrecompileTarget`, `isFiscoPrecompileAddress`)

**Interfaces:**
- Produces: `FiscoChainCallTargetAdapter` composing `ExecutorPrecompileAdapter<SessionContext>&` for `dispatch`

- [ ] **Step 1: Implement classifyTarget using executionAddress normative rules (spec §4.3)**
- [ ] **Step 2: dispatch delegates to existing `ExecutorPrecompileAdapter::dispatch`**
- [ ] **Step 3: forEachStaticWarmTarget no-op**
- [ ] **Step 4: TE smoke test `FiscoChainCallTargetAdapterTest.cpp` (one case: 0x1003 hit)**
- [ ] **Step 5: Update `transaction-executor/CMakeLists.txt` if adapters not GLOBbed**

---

### Task 6: Wire chainPort fields (unused in ExecutionFrame until PR4)

**Files:**
- Modify: `bcos-evm/eth/execution/InnerExecute.h` — add `#include "bcos-evm/eth/ports/ChainCallTargetPort.h"` forward decl + `ChainCallTargetPort* chainPort{nullptr};`
- Modify: `bcos-evm/eth/execution/EvmCallFrame.h` — add `ChainCallTargetPort* chainPort{nullptr};` to `FrameContext` ctor
- Modify: `bcos-evm/bcos/FiscoExecutionBridge.cpp` — construct `FiscoChainCallTargetAdapter` on stack, set `input.chainPort`
- Modify: `bcos-evm/opstack/OpStackTxLifecycle.cpp` — construct `OpStackChainCallTargetAdapter`, store on `TxPipelineContext` or parallel field (add `ChainCallTargetPort* chainPort` to context if needed)
- Modify: `bcos-evm/eth/execution/TxExecutionAdapter.cpp` — pass `input.chainPort` when building `FrameContext` (still uses legacy `tryPrecompileDispatch` until PR4)

- [ ] **Step 1: Add fields + compile all targets**
- [ ] **Step 2: Run existing tests — no behavior change**

Run: `ctest --test-dir build/bcos-evm/test -R 'ExecutionFrame|PrecompileRouter|FiscoExecution' --output-on-failure`
Expected: all PASS (same as before)

- [ ] **Step 3: Commit PR3**

---

## PR4 — Behavior switch (ExecutionFrame + envelope)

### Task 7: executePrecompileEnvelope

**Files:**
- Modify: `bcos-evm/eth/precompiled/PrecompileRouter.h`
- Modify: `bcos-evm/eth/precompiled/PrecompileRouter.cpp`

**Interfaces:**
- Consumes: `CallTargetDescriptor` with kind Builtin or Chain
- Produces: `executePrecompileEnvelope(PrecompileEnvelopeInput)`

- [ ] **Step 1: Write failing envelope test**

Create `bcos-evm/test/eth/PrecompileEnvelopeTest.cpp`:

```cpp
#define BOOST_TEST_MODULE PrecompileEnvelopeTest

#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(builtin_envelope_dispatches_ecrecover)
{
    bcos::evm::test::InMemoryEvmStateReader base;
    bcos::evm::state::State state{base};
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_CANCUN;

    evmc_address target{};
    target.bytes[19] = 0x01;
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.gas = 500000;

    bcos::evm::execution::CallTargetDescriptor desc{
        .kind = bcos::evm::execution::CallTargetKind::BuiltinPrecompile,
        .dispatchAddress = target,
        .warmPolicy = bcos::evm::execution::WarmPolicy::TxEntryAlways,
        .routed = msg,
    };

    auto out = bcos::evm::precompiled::executePrecompileEnvelope({
        .state = state,
        .revision = cfg,
        .target = desc,
        .message = msg,
        .skipValueTransfer = false,
        .chainPort = nullptr,
    });

    BOOST_CHECK(out.outcome == bcos::evm::precompiled::PrecompileDispatchOutcome::Dispatched);
}
```

- [ ] **Step 2: Extract envelope body from `dispatchPrecompile` into `executePrecompileEnvelope`**
- [ ] **Step 3: Leave `dispatchPrecompile` as deprecated wrapper calling resolve + envelope (for dual-run) OR delete after switch**
- [ ] **Step 4: `ctest -R PrecompileEnvelope -V` PASS**

---

### Task 8: ExecutionFrame PR4 delta

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`
- Modify: `bcos-evm/eth/state/EthHost.cpp` — pass `ctx.chainPort` into nested frame
- Modify: `bcos-evm/eth/execution/TxExecutionAdapter.cpp` — set `FrameContext.chainPort`

**Interfaces:**
- Implements spec §5.1 flow

- [ ] **Step 1: Replace `tryPrecompileDispatch` body:**

```cpp
// Pseudocode — implement in ExecutionFrame.cpp
if (!isCreateKind(callMessage.kind)) {
    auto frameTarget = resolveFrameTarget(...);  // already in FrameWork
    bool skipVt = extension && extension->skipHostValueTransfer();
    auto desc = resolveCallTarget(state, revision, frameTarget.routed, scope,
                                  ctx.chainPort, ctx.extension);
    switch (desc.kind) {
    case BuiltinPrecompile:
    case ChainPrecompile:
        return envelopeFrameResult(executePrecompileEnvelope({...}));
    case EmptyAccount:
        return emptyAccountFrameResult(...);  // §4.5 table
    case PolicyRejected:
        return FrameResult{makePrecompileFailureResult(...), .precompileHit=false};
    case EvmContract:
        break;
    }
}
// RR6: only after this block → prepareNestedMessage for Nested
```

- [ ] **Step 2: Remove duplicate `resolveFrameTarget` if FrameWork already holds target**
- [ ] **Step 3: Migrate characterization test**

Copy `test/cross/PrecompileRouterCharacterizationTest.cpp` → `test/cross/CallTargetCharacterizationTest.cpp`; update CMake in `CrossTests.cmake`; wire `chainPort` for FISCO/OpStack cases.

- [ ] **Step 4: PR4 regression gate**

Run:
```bash
ctest -R 'CallTargetCharacterization|CallTargetResolver|PrecompileEnvelope|ExecutionFrame|FrameTarget' -V
```
Expected: PASS including C7 depth asymmetry case.

- [ ] **Step 5: Strip `tryChainPrecompile` bodies from `FiscoVmHostPolicy` / `OpStackVmHostPolicy` (keep deprecated forward if needed)**

- [ ] **Step 6: Commit PR4**

---

## PR5 — Tx-entry warm (conditional behavior)

### Task 9: warmTransactionEntry + OpStack static warm

**Files:**
- Modify: `bcos-evm/eth/execution/WarmTransactionEntry.h`
- Modify: `bcos-evm/eth/execution/TxExecutionAdapter.cpp` — pass `chainPort` into `warmTransactionEntry` (extend signature)
- Modify: `bcos-evm/test/state/WarmTransactionEntryTest.cpp`

- [ ] **Step 1: Record C2 OpStack L1Block gas baseline OR document op-geth warm rule — gate check**
- [ ] **Step 2: Replace `forEachActivePrecompile` block with `enumerateTxEntryWarmTargets(cfg, chainPort, ...)`**
- [ ] **Step 3: Extend `warmTransactionEntry` signature:**

```cpp
inline void warmTransactionEntry(state::State& state,
    bcos::evm_standard::RevisionConfig const& cfg,
    ChainCallTargetPort const* chainPort,
    const state::Transaction& tx, ...);
```

- [ ] **Step 4: Update all call sites (TxExecutionAdapter, tests)**
- [ ] **Step 5: W2 test — OpStack port adds L1 + GasOracle to warm set**
- [ ] **Step 6: If geth evidence missing — merge with `chainPort` plumbed but OpStack `forEachStaticWarmTarget` empty until evidence**

---

## PR6 — Cleanup + docs

### Task 10: Remove deprecated code and update docs

**Files:**
- Delete: `bcos-evm/bcos/ports/ChainPrecompilePort.h` (update TE to use `ChainCallTargetPort` only)
- Delete: `bcos-evm/eth/execution/FrameTargetResolver.cpp` if fully inlined (keep header as delegate or delete)
- Delete: `test/eth/PrecompileRouterPrecedenceTest.cpp`, `test/cross/PrecompileRouterCharacterizationTest.cpp`
- Modify: `bcos-evm/docs/architecture-overview.md` — ADR 001–024
- Modify: `docs/superpowers/specs/2026-06-24-execution-frame-design.md` — §2.2 chainPort
- Modify: `bcos-evm/docs/adr/005-orchestration-domain-boundaries.md` — §3 table
- Modify: `bcos-evm/docs/adr/017-fisco-precompile-port.md` — extended by ADR-024
- Modify: `bcos-evm/docs/adr/024-call-target-resolver-deepening.md` — Status → Accepted

- [ ] **Step 1: Remove `VmHostPolicy::tryChainPrecompile` default + overrides**
- [ ] **Step 2: Remove deprecated `dispatchPrecompile` if envelope-only path stable**
- [ ] **Step 3: Update CMake test registrations**
- [ ] **Step 4: Run full bcos-evm + TE tests**
- [ ] **Step 5: CI grep gates (spec §9.1)**
- [ ] **Step 6: Mark ADR-024 Accepted; spec status 已批准**

---

## Spec Coverage Self-Review

| Spec section | Task |
| --- | --- |
| §4.1 file layout | Tasks 1, 3, 4, 5, 7, 8 |
| §4.2 types | Task 1 |
| §4.3 ChainCallTargetPort + adapters | Tasks 4, 5 |
| §4.4 envelope 4a | Task 7 |
| §4.5 classification + EmptyAccount | Task 3, 8 |
| §4.6 warm 2b+ | Task 9 |
| §5.1 PR4 delta RR6/RR7 | Task 8 |
| §5.4 injection matrix | Task 6, 8 |
| §7 tests 5b | Tasks 3, 7, 8 |
| §9 PR4/PR5 gates | Tasks 8, 9 |
| §10 risks | Dual-run optional in Task 3 Step 3 note; C7 in Task 8 |

**Gap closed in plan:** PR1 `ChainPrecompilePort` inheritance deferred to PR3 to avoid breaking `ExecutorPrecompileAdapter` mid-flight.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-26-call-target-resolver.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per PR/task, review between tasks, fast iteration. Use `superpowers:subagent-driven-development`.

2. **Inline Execution** — implement PR1→PR6 in this session with checkpoints after each PR. Use `superpowers:executing-plans`.

**Which approach?**
