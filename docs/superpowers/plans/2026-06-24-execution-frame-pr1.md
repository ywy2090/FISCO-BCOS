# ExecutionFrame PR1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the `ExecutionFrame` deep module and delegate `EthHost::call` to `runExecutionFrame(Nested)` while leaving `executeMessage` unchanged (temporary tx-entry dual-track).

**Architecture:** Extract frame helpers into `bcos-evm/eth/execution/` (`RouteMessage`, `FrameValueTransfer`, `ResolveExecutionCode`, `ExecutionFrame`). Implement scope-aware pipeline with frozen per-scope ordering (RR6: Nested ③→②; RR7: TopLevel CREATE ⑤→④, Nested CREATE ④→⑤). `EthHost::call` becomes a thin adapter; `executeMessage.cpp` is untouched in PR1.

**Tech Stack:** C++20, evmc, evmone, Boost.Test, CMake/CTest, `bcos-evm-eth` static library.

**Spec:** `docs/superpowers/specs/2026-06-24-execution-frame-design.md` (§9 PR1, §4 RR6/RR7, §8.1)

**Follow-up plans (out of scope here):** PR2 `executeMessage` migration, PR3 docs/cleanup — separate plans.

## Global Constraints

- `eth/execution/` must **never** `#include` `bcos/` or `opstack/` headers (ADR-005 / ADR-019).
- Chain behavior enters Frame only via `FrameContext::extension` (`state::EvmHostHooks*`).
- `FrameScope` is passed by the adapter; Frame must **not** branch on `message.depth` for semantics (trace/logging only).
- PR1 **freezes** Nested execution order exactly as current `EthHost::call` (RR6, RR3).
- PR1 **freezes** TopLevel CREATE order as checkpoint → bindCreate (RR7); Nested CREATE as bindCreate → checkpoint (RR7).
- `runExecutionFrame` is synchronous and does **not** catch exceptions.
- Nested adapter (`EthHost::call`) **ignores** `FrameResult::gasRefund` (RR4).
- `PrecompileRouter::dispatchPrecompile` envelope is unchanged; PR1 makes Frame the call site for **nested** frames only (`executeMessage` direct call remains until PR2).
- `TxPipelineHooks::txRunEvmExecutionOverride` unchanged.

### Build & Test Conventions

- Run from repo root: `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`
- Configure once: `cmake -B build -DTESTS=ON 2>&1 | rtk err`
- Build library: `cmake --build build --target bcos-evm-eth -j8 2>&1 | rtk err`
- Build one test: `cmake --build build --target <TargetName> -j8 2>&1 | rtk err`
- Run one ctest: `ctest --test-dir build -R "^<CTestName>$" --output-on-failure 2>&1 | rtk err`
- Full eth + cross gate: `ctest --test-dir build -R "PrecompileRouter|Eip|Eth|Tx|ExecuteMessage|DebitIntrinsic|ExecutionFrame|RouteMessage|FrameValueTransfer|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err`

### File Map (PR1)

| Path | Responsibility |
| --- | --- |
| `bcos-evm/eth/execution/Delegation7702Frame.h` | `isDirectDelegated7702` / `isDelegated7702Message` — shared by RouteMessage + ExecutionFrame |
| `bcos-evm/eth/execution/FrameScope.h` | `enum class FrameScope { TopLevel, Nested }` |
| `bcos-evm/eth/execution/RouteMessage.h` / `.cpp` | `RoutedMessage`, `routeMessage(...)` — extracted from `EthHost::routeCall` |
| `bcos-evm/eth/execution/FrameValueTransfer.h` | `transferFrameValue(...)` — merged nested `transferValue` + top-level CALL/CREATE endowment semantics |
| `bcos-evm/eth/execution/FrameCaller.h` | `resolveCallerAddress(evmc_address const& executionAddress, evmc_message const& msg)` |
| `bcos-evm/eth/execution/EvmCallFrame.h` | `FrameScope`, `FrameContext`, `FrameResult`, `runExecutionFrame` declaration |
| `bcos-evm/eth/execution/ExecutionFrame.cpp` | 9-step pipeline; **only** nested production call site for `dispatchPrecompile` among live adapters |
| `bcos-evm/eth/state/EthHost.hpp` | Add `execution_address_ref()`; remove `routeCall`/`resolveExecutionCode`/`transferValue` declarations |
| `bcos-evm/eth/state/EthHost.cpp` | Thin `call()` adapter delegating to Frame |
| `bcos-evm/test/eth/RouteMessageTest.cpp` | Unit tests for routing / precompile target / CREATE warm-pin |
| `bcos-evm/test/eth/ExecutionFrameTest.cpp` | Nested equivalence + TopLevel characterization + parity matrix |
| `bcos-evm/eth/execution/ResolveExecutionCode.h` | `resolveExecutionCode(...)` — extracted from `EthHost::resolveExecutionCode` |
| `bcos-evm/test/cmake/StateTests.cmake` | **PR1:** 6 targets vendoring `EthHost.cpp` → link `bcos-evm-eth` |

Existing helpers in `eth/execution/` (`WarmTransactionEntry.h`, etc.) are tx-level; do not modify unless a compile error forces an include path fix.

**Library sources:** `bcos-evm/CMakeLists.txt` uses `GLOB_RECURSE eth/*.cpp` — new `eth/execution/*.cpp` files are picked up automatically; no CMake edit required for `bcos-evm-eth`.

---

### Task 1: `EthHost::execution_address_ref` accessor

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.hpp` (after `set_execution_address`)
- Modify: `bcos-evm/eth/state/EthHost.cpp` (no logic change)

**Interfaces:**
- Produces: `evmc_address& EthHost::execution_address_ref() noexcept { return m_executionAddress; }`

- [ ] **Step 1: Add accessor to header**

In `bcos-evm/eth/state/EthHost.hpp`, after `set_execution_address`:

```cpp
evmc_address& execution_address_ref() noexcept { return m_executionAddress; }
```

- [ ] **Step 2: Build library**

Run: `cmake --build build --target bcos-evm-eth -j8 2>&1 | rtk err`  
Expected: PASS (no behavior change)

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/eth/state/EthHost.hpp
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): Add EthHost execution_address_ref accessor for Frame adapter

EOF
)"
```

---

### Task 2: Extract `routeMessage` from `EthHost::routeCall`

**Files:**
- Create: `bcos-evm/eth/execution/Delegation7702Frame.h`
- Create: `bcos-evm/eth/execution/RouteMessage.h`
- Create: `bcos-evm/eth/execution/RouteMessage.cpp`
- Create: `bcos-evm/test/eth/RouteMessageTest.cpp`
- Modify: `bcos-evm/test/cmake/EthTests.cmake`
- Reference (copy from): `bcos-evm/eth/state/EthHost.cpp:584-635` and anonymous helpers `isDirectDelegated7702`, `isDelegated7702Message`

**Interfaces:**
- Produces:

```cpp
namespace bcos::evm::execution {

struct RoutedMessage {
    evmc_message message{};
    evmc_address precompileTarget{};
    bool hasPrecompileTarget{false};
};

RoutedMessage routeMessage(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message msg,
    FrameScope scope);

}  // namespace bcos::evm::execution
```

- Consumes: `FrameScope` from `ExecutionFrame.h` — create a minimal forward header first:

```cpp
// bcos-evm/eth/execution/FrameScope.h
#pragma once
namespace bcos::evm::execution {
enum class FrameScope { TopLevel, Nested };
}
```

**Scope behavior (spec §4.1 / §4.5):**
- **Nested:** full current `routeCall` logic (CREATE recipient/code_address fill + warm-pin, 7702 target, precompile target flag).
- **TopLevel CALL:** `zero code_address → recipient` only.
- **TopLevel CREATE:** **skip** recipient/code_address 互填与 warm-pin（caller 已填 `message.recipient`）；不 mutate `code_address`。

- [ ] **Step 1: Create `FrameScope.h`**

Create `bcos-evm/eth/execution/FrameScope.h` with the enum above.

- [ ] **Step 2: Write failing routing tests**

Create `bcos-evm/test/eth/RouteMessageTest.cpp`:

```cpp
#define BOOST_TEST_MODULE RouteMessageTest

#include "bcos-evm/eth/execution/RouteMessage.h"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}

bcos::evm_standard::RevisionConfig pragueCfg()
{
    return {.revision = EVMC_PRAGUE, .warm_access = true};
}
}  // namespace

BOOST_AUTO_TEST_CASE(nested_create_fills_recipient_and_pins_warm)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.recipient = addr(0x42);
    msg.code_address = {};

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::Nested);
    BOOST_REQUIRE(
        std::memcmp(routed.message.code_address.bytes, addr(0x42).bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(nested_marks_identity_precompile_target)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = identity;
    msg.code_address = identity;

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::Nested);
    BOOST_REQUIRE(routed.hasPrecompileTarget);
    BOOST_REQUIRE(std::memcmp(routed.precompileTarget.bytes, identity.bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(top_level_skips_create_warm_pin)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.recipient = addr(0x55);
    msg.code_address = {};

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(std::memcmp(routed.message.recipient.bytes, addr(0x55).bytes, 20) == 0);
    // TopLevel: no recipient/code_address mutation beyond caller-provided fields
    BOOST_REQUIRE(std::memcmp(routed.message.code_address.bytes, evmc_address{}.bytes, 20) == 0);
}
}  // namespace bcos::evm::test
```

Register in `bcos-evm/test/cmake/EthTests.cmake`:

```cmake
add_executable(RouteMessageTest eth/RouteMessageTest.cpp)
target_include_directories(RouteMessageTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(RouteMessageTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME RouteMessage COMMAND RouteMessageTest)
```

- [ ] **Step 3: Run tests — expect FAIL**

Run: `cmake --build build --target RouteMessageTest -j8 2>&1 | rtk err && ctest --test-dir build -R "^RouteMessage$" --output-on-failure 2>&1 | rtk err`  
Expected: FAIL — `routeMessage` not defined / link error

- [ ] **Step 4: Create `Delegation7702Frame.h` and implement `RouteMessage`**

Extract `isDirectDelegated7702` / `isDelegated7702Message` from `EthHost.cpp` anonymous namespace into `Delegation7702Frame.h` (inline). `RouteMessage.cpp` and `ExecutionFrame.cpp` **both include** this header (do not use anonymous namespace — cross-TU visibility required).

Move logic verbatim from `EthHost::routeCall` into `routeMessage`, parameterized by `FrameScope`. Use `precompiled::isActivePrecompile(revisionConfig, target)`.

- [ ] **Step 5: Run tests — expect PASS**

Run: `ctest --test-dir build -R "^RouteMessage$" --output-on-failure 2>&1 | rtk err`  
Expected: PASS (3 cases)

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/execution/FrameScope.h bcos-evm/eth/execution/Delegation7702Frame.h \
  bcos-evm/eth/execution/RouteMessage.h bcos-evm/eth/execution/RouteMessage.cpp \
  bcos-evm/test/eth/RouteMessageTest.cpp bcos-evm/test/cmake/EthTests.cmake
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): Extract routeMessage from EthHost for ExecutionFrame

EOF
)"
```

---

### Task 3: Extract `resolveExecutionCode` and `FrameValueTransfer`

**Files:**
- Create: `bcos-evm/eth/execution/ResolveExecutionCode.h`
- Create: `bcos-evm/eth/execution/FrameValueTransfer.h`
- Create: `bcos-evm/eth/execution/FrameCaller.h`
- Create: `bcos-evm/test/eth/FrameValueTransferTest.cpp`
- Modify: `bcos-evm/test/cmake/EthTests.cmake`
- Reference: `EthHost.cpp:637-665` (`resolveExecutionCode`), `EthHost.cpp:667-687` (`transferValue`), `ExecuteMessage.cpp:114-136` (`applyTopLevelValueTransfer` CREATE/CALL rules)

**Interfaces:**
- Produces:

```cpp
// ResolveExecutionCode.h
bcos::bytes resolveExecutionCode(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message const& msg);

// FrameCaller.h
evmc_address resolveCallerAddress(evmc_address const& executionAddress, evmc_message const& msg) noexcept;

// FrameValueTransfer.h
bool transferFrameValue(state::State& state, bcos::evm_standard::RevisionConfig const& revisionConfig,
    state::EvmHostHooks* extension, evmc_message const& msg, execution::FrameScope scope) noexcept;
```

- [ ] **Step 1: Write failing transfer test**

Create `bcos-evm/test/eth/FrameValueTransferTest.cpp`:

```cpp
#define BOOST_TEST_MODULE FrameValueTransferTest

#include "bcos-evm/eth/execution/FrameValueTransfer.h"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(nested_insufficient_balance_returns_false)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.sender = evmc_address{.bytes = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
    msg.recipient = evmc_address{.bytes = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}};
    msg.value = evmc_uint256be{.bytes = {[31] = 100}};

    state.set_balance(msg.sender, 50);
    bool ok = execution::transferFrameValue(state, cfg, nullptr, msg, execution::FrameScope::Nested);
    BOOST_REQUIRE(!ok);
}
}  // namespace bcos::evm::test
```

Register in `bcos-evm/test/cmake/EthTests.cmake`:

```cmake
add_executable(FrameValueTransferTest eth/FrameValueTransferTest.cpp)
target_include_directories(FrameValueTransferTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FrameValueTransferTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME FrameValueTransfer COMMAND FrameValueTransferTest)
```

- [ ] **Step 2: Run test — expect FAIL**

Run: `cmake --build build --target FrameValueTransferTest -j8 2>&1 | rtk err && ctest --test-dir build -R "^FrameValueTransfer$" --output-on-failure 2>&1 | rtk err`

- [ ] **Step 3: Implement helpers**

- `resolveExecutionCode`: move body from `EthHost::resolveExecutionCode`; use `resolve7702CodeAddress` logic inline or as file-local helper (copy from `EthHost.cpp` anonymous namespace).
- `resolveCallerAddress`: copy from `EthHost::resolveCallerAddress` but take `executionAddress` parameter instead of `m_executionAddress`.
- `transferFrameValue`:
  - `FrameScope::Nested`: current `EthHost::transferValue` behavior.
  - `FrameScope::TopLevel`: current `applyTopLevelValueTransfer` for CALL; for CREATE, endowment check + `transfer` (same as `ExecuteMessage.cpp:260-271`).

- [ ] **Step 4: Run test — expect PASS**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/execution/ResolveExecutionCode.h bcos-evm/eth/execution/FrameValueTransfer.h \
  bcos-evm/eth/execution/FrameCaller.h bcos-evm/test/eth/FrameValueTransferTest.cpp \
  bcos-evm/test/cmake/EthTests.cmake
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): Extract frame value transfer and execution code resolution

EOF
)"
```

---

### Task 4: `ExecutionFrame` public API

**Files:**
- Create: `bcos-evm/eth/execution/EvmCallFrame.h`
- Modify: `bcos-evm/eth/execution/FrameScope.h` — either merge enum into `ExecutionFrame.h` or keep include

**Interfaces:**
- Produces (spec §3.2):

```cpp
namespace bcos::evm::execution {

struct FrameContext {
    state::State& state;
    evmc::VM& vm;
    bcos::evm_standard::RevisionConfig const& revisionConfig;
    state::EvmHostHooks* extension{nullptr};
    evmc_address txOrigin{};
    evmc_address& executionAddress;
    bool fixNonceInit{false};

    FrameContext(state::State& state_, evmc::VM& vm_,
        bcos::evm_standard::RevisionConfig const& revisionConfig_, state::EvmHostHooks* extension_,
        evmc_address txOrigin_, evmc_address& executionAddress_, bool fixNonceInit_ = false) noexcept
      : state(state_), vm(vm_), revisionConfig(revisionConfig_), extension(extension_),
        txOrigin(txOrigin_), executionAddress(executionAddress_), fixNonceInit(fixNonceInit_)
    {}
};

struct FrameResult {
    evmc::Result result{evmc_result{}};
    int64_t gasRefund{0};
    bool precompileHit{false};
};

FrameResult runExecutionFrame(FrameContext& ctx, evmc_message message, FrameScope scope,
    state::EthHost& host);

}  // namespace bcos::evm::execution
```

- [ ] **Step 1: Add header with forward declare**

`ExecutionFrame.h` forward-declares `bcos::evm::state::EthHost`; no `.cpp` yet.

- [ ] **Step 2: Create stub `ExecutionFrame.cpp`**

```cpp
#include "bcos-evm/eth/execution/EvmCallFrame.h"
#include "bcos-evm/eth/state/EthHost.hpp"

namespace bcos::evm::execution {

FrameResult runExecutionFrame(FrameContext& ctx, evmc_message message, FrameScope scope,
    state::EthHost& host)
{
    (void)ctx;
    (void)message;
    (void)scope;
    (void)host;
    evmc_result raw{};
    raw.status_code = EVMC_INTERNAL_ERROR;
    return FrameResult{.result = evmc::Result(raw)};
}

}  // namespace bcos::evm::execution
```

- [ ] **Step 3: Build library**

`ExecutionFrame.cpp` is auto-included via `GLOB_RECURSE eth/*.cpp` — no `bcos-evm/CMakeLists.txt` edit.

Run: `cmake --build build --target bcos-evm-eth -j8 2>&1 | rtk err`  
Expected: PASS

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/execution/EvmCallFrame.h bcos-evm/eth/execution/ExecutionFrame.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): Add ExecutionFrame public API stub

EOF
)"
```

---

### Task 5: Implement `runExecutionFrame` — Nested scope (production path)

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp` (full pipeline)
- Modify: `bcos-evm/eth/state/EthHost.cpp` — delegate `call()`; delete moved helpers
- Modify: `bcos-evm/eth/state/EthHost.hpp` — remove `routeCall`/`resolveExecutionCode`/`transferValue`/`resolveCallerAddress` declarations
- Modify: `bcos-evm/test/cmake/StateTests.cmake` — link `bcos-evm-eth` for vendored-EthHost targets

**Interfaces:**
- Consumes: all Task 2–4 symbols
- Produces: behavior-identical `EthHost::call` for `depth >= 1`

**Nested execution order (RR6 — must match current `EthHost::call` verbatim):**

```text
① routeMessage(Nested)
② DELEGATECALL→precompile guard (uses routed.hasPrecompileTarget)
③ tryPrecompile (dispatchPrecompile) — early return on hit; set precompileHit + gasRefund
② resolveCallerAddress + setCallerAddress + prepareMessage  (after ③)
④ bindCreateMessageForInit (CREATE only)
⑤ checkpoint
⑥ initializeCreateTargetAccount (CREATE only)
⑦ transferFrameValue(Nested)
⑧ resolveExecutionCode + vm.execute
⑨ finalize: codeDepositGas, installCreatedContractCode, markCreatedInTx,
            commit/revert, update executionAddress (non-CREATE success),
            Nested CREATE attempt nonce bump + bumpContractCreateNonce (no success check — spec §4.1)
```

- [ ] **Step 1: Implement Nested branch in `ExecutionFrame.cpp`**

Copy finalize logic from `EthHost::call` lines 339-406. Use `host.markCreatedInTx`, `host` for `vm.execute`. On DELEGATECALL guard failure return `EVMC_PRECOMPILE_FAILURE` with message gas (no main checkpoint).

Before prepareMessage:

```cpp
auto const callerAddress =
    resolveCallerAddress(ctx.executionAddress, routed.message);
if (ctx.extension != nullptr)
{
    ctx.extension->setCallerAddress(callerAddress);
    ctx.extension->prepareMessage(ctx.revisionConfig.revision, routed.message);
}
```

Include `Delegation7702Frame.h` for precompile guard. Precompile step:

```cpp
if (!state::isCreateKind(routed.message.kind) &&
    !(isDelegated7702Message(originalMsg) && routed.message.kind != EVMC_CALL))
{
    bool const skipVt = ctx.extension != nullptr && ctx.extension->skipHostValueTransfer();
    auto const target = state::isZeroAddress(routed.message.code_address) ?
                            routed.message.recipient :
                            routed.message.code_address;
    auto out = precompiled::dispatchPrecompile(
        {ctx.state, ctx.revisionConfig, ctx.extension, routed.message, target, skipVt});
    if (out.outcome != precompiled::PrecompileDispatchOutcome::NotApplicable)
    {
        return FrameResult{.result = std::move(out.result),
            .gasRefund = out.gasRefund,
            .precompileHit = true};
    }
}
```

- [ ] **Step 2: Wire thin `EthHost::call`**

Replace body of `EthHost::call` with:

```cpp
EthHost::Result EthHost::call(const evmc_message& msg) noexcept
{
    // ... existing depth>0 trace ...
    struct ExecutionAddressGuard { /* unchanged */ };
    ExecutionAddressGuard guard{m_executionAddress};

    execution::FrameContext frameCtx{m_state, m_vm, m_revisionConfig, m_extension,
        m_txContext.tx_origin, m_executionAddress};
    auto fr = execution::runExecutionFrame(frameCtx, msg, execution::FrameScope::Nested, *this);
    return Result(std::move(fr.result));
}
```

- [ ] **Step 3: Delete dead code from `EthHost`**

Remove `routeCall`, `resolveExecutionCode`, `transferValue`, **`resolveCallerAddress`** methods and `RoutedCall` struct from `EthHost.hpp/.cpp`.

- [ ] **Step 3b: Fix `StateTests.cmake` vendored `EthHost.cpp` targets**

These 6 targets inline `../eth/state/EthHost.cpp` without linking `bcos-evm-eth`. After Task 5, they will fail to link `runExecutionFrame`.

Targets (ctest NAME → binary): `PragueState`, `NestedCallHost`, `PrecompileInCall`, `BlockHashHost`, `NestedRevertWarm`, `EvmoneRefundSpike`.

For each, **remove** `../eth/state/EthHost.cpp` (and other sources already in `bcos-evm-eth`) from `add_executable`, and add:

```cmake
target_link_libraries(${TARGET} PRIVATE bcos-evm-eth evmone::evmone ...)
```

Keep any test-specific sources not in the static library. Verify:

```bash
cmake --build build --target NestedCallHostTest PragueStateTest -j8 2>&1 | rtk err
```

- [ ] **Step 4: Run nested equivalence oracle**

Run: `ctest --test-dir build -R "^PrecompileRouterEnvelope$" --output-on-failure 2>&1 | rtk err`  
Expected: PASS (2 cases)

Run: `ctest --test-dir build -R "^PrecompileRouter" --output-on-failure 2>&1 | rtk err`  
Expected: PASS

Run: `ctest --test-dir build -R "^PrecompileRouterCharacterization$|^PrecompileRouterEquivalence$" --output-on-failure 2>&1 | rtk err`  
Expected: PASS (cross depth1 oracle — spec §8.1)

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/execution/ExecutionFrame.cpp bcos-evm/eth/state/EthHost.cpp \
  bcos-evm/eth/state/EthHost.hpp bcos-evm/test/cmake/StateTests.cmake
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): Delegate EthHost::call to runExecutionFrame(Nested)

EOF
)"
```

---

### Task 6: Implement `runExecutionFrame` — TopLevel scope (characterization only)

**Files:**
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`

**TopLevel execution order (RR7 — must match current `executeMessage` frame body):**

```text
① routeMessage(TopLevel)   # CALL: zero code_address→recipient; CREATE: skip 互填
③ tryPrecompile (empty-code path only — mirror executeMessage pre-check)
⑤ checkpoint
⑦ transferFrameValue(TopLevel) — CALL only; CREATE skips CALL transfer
④ bindCreateMessageForInit + CREATE endowment (RR7: checkpoint before bind)
⑥ initializeCreateTargetAccount (CREATE)
⑧ vm.execute
⑨ finalize — no sender nonce bump; fixNonceInit when ctx.fixNonceInit
```

Note: TopLevel branch is **not** wired to `executeMessage` in PR1; it exists for direct `ExecutionFrameTest` characterization (spec §8.1, R8).

- [ ] **Step 1: Add TopLevel branch with RR7 ordering**

Entry: `auto routed = routeMessage(ctx.state, ctx.revisionConfig, message, FrameScope::TopLevel)`.

For non-CREATE precompile: resolve code via `resolveExecutionCode`; if empty, run step ③ before checkpoint (same as `ExecuteMessage.cpp:224-242`).

Do **not** call `prepareMessage` (spec §4.1).

- [ ] **Step 2: Build library**

Run: `cmake --build build --target bcos-evm-eth -j8 2>&1 | rtk err`

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/eth/execution/ExecutionFrame.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): Add TopLevel scope to runExecutionFrame for characterization

EOF
)"
```

---

### Task 7: `ExecutionFrameTest` — parity matrix + guard regressions

**Files:**
- Create: `bcos-evm/test/eth/ExecutionFrameTest.cpp`
- Modify: `bcos-evm/test/cmake/EthTests.cmake`

**Interfaces:**
- Consumes: `runExecutionFrame`, `FrameContext`, helpers from Tasks 1–6
- Reuses patterns from `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp` (`runDepth0`/`runDepth1` oracle)

- [ ] **Step 1: Write failing parity tests**

Create `bcos-evm/test/eth/ExecutionFrameTest.cpp` with at minimum:

1. **`nested_precompile_insufficient_balance_matches_envelope_test`** — copy envelope test depth1 case; oracle = `PrecompileRouterEnvelopeTest` `runDepth1` outcomes (status, gas, balances).

2. **`nested_successful_value_transfer_matches_envelope_test`** — same pattern for successful transfer (envelope depth1 oracle).

3. **`nested_delegatecall_precompile_blocked`** — `EVMC_DELEGATECALL` to identity precompile with `allowDelegateCallToPrecompile() == false`; expect `EVMC_PRECOMPILE_FAILURE`.

4. **`nested_7702_delegatecall_precompile_guard`** — nested 7702 DELEGATECALL to precompile target; expect guard blocks per §4.2 (spec §8.1).

5. **`top_level_precompile_hit_sets_precompileHit`** — direct `runExecutionFrame(TopLevel)` on identity precompile CALL; expect `fr.precompileHit == true` and `EVMC_SUCCESS`.

6. **`top_level_create_checkpoint_before_bind_order`** — RR7 smoke: failed endowment after checkpoint → `EVMC_INSUFFICIENT_BALANCE`.

Register:

```cmake
add_executable(ExecutionFrameTest eth/ExecutionFrameTest.cpp)
target_include_directories(ExecutionFrameTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(ExecutionFrameTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME ExecutionFrame COMMAND ExecutionFrameTest)
```

Helper to build `FrameContext` + `EthHost` (mirror `Depth1HostFixture` from envelope test):

```cpp
struct FrameTestHost {
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .warm_access = true};
    state::EthHost host;

    explicit FrameTestHost(state::State& state)
      : host(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, false)
    {}
};
```

- [ ] **Step 2: Run tests — expect FAIL then PASS**

Run: `cmake --build build --target ExecutionFrameTest -j8 2>&1 | rtk err && ctest --test-dir build -R "^ExecutionFrame$" --output-on-failure 2>&1 | rtk err`

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/test/eth/ExecutionFrameTest.cpp bcos-evm/test/cmake/EthTests.cmake
rtk git commit -m "$(cat <<'EOF'
test(bcos-evm): Add ExecutionFrame PR1 gate tests

EOF
)"
```

---

### Task 8: Seam audit + full PR1 regression gate

**Files:**
- Verify only (no new files required)

- [ ] **Step 1: Seam audit — no forbidden includes**

Run:

```bash
rg '#include "bcos-evm/(bcos|opstack)/' bcos-evm/eth/execution 2>&1 | rtk err
```

Expected: **no matches**

- [ ] **Step 2: Confirm `executeMessage` still compiles unchanged**

Run: `ctest --test-dir build -R "^ExecuteMessageSmoke$" --output-on-failure 2>&1 | rtk err`  
Expected: same pass/fail as before PR1 (known nonce debt may still fail — not a PR1 regression)

- [ ] **Step 3: Full eth + cross unit gate**

Run: `ctest --test-dir build -R "PrecompileRouter|Eip|Eth|Tx|ExecuteMessage|DebitIntrinsic|ExecutionFrame|RouteMessage|FrameValueTransfer|PrecompileRouterCharacterization|PrecompileRouterEquivalence" --output-on-failure 2>&1 | rtk err`

- [ ] **Step 3b: StateTests vendored-EthHost targets still build**

Run: `cmake --build build --target NestedCallHostTest PragueStateTest -j8 2>&1 | rtk err`

- [ ] **Step 4: FISCO / OpStack smoke (spec §8.3)**

Run:

```bash
ctest --test-dir build -R "FiscoExecutionBridgeSmoke|Bcos7702|Bcos7212|OpStackExecutionBridgeSmoke" --output-on-failure 2>&1 | rtk err
```

- [ ] **Step 5: Final commit if audit fixes needed**

Only if Step 1 found violations or test CMake needed fixes.

---

## Self-Review (spec coverage)

| Spec requirement | Task |
| --- | --- |
| §3.1 File layout | Tasks 2–4 |
| §3.2 `FrameContext` / `FrameResult` / no default ctor | Task 4 |
| §4 RR6 Nested ③→② | Task 5 |
| §4 RR7 CREATE order per scope | Tasks 5–6 |
| §4.2 unified precompile guard | Task 5 |
| §5.2 thin `EthHost::call` | Task 5 |
| §5.1 `executeMessage` unchanged | Task 8 Step 2 |
| §6 Frame sole nested `dispatchPrecompile` call site | Task 5 (executeMessage retains its call until PR2) |
| §7 error table behaviors | Tasks 5–6 (preserve existing status codes) |
| §8.1 PR1 test gate | Task 7 |
| §8.3 regression gate | Task 8 |
| R1 `execution_address_ref` | Task 1 |
| RR2 delete old EthHost helpers | Task 5 Step 3 (+ `resolveCallerAddress`) |
| R10 `FrameCaller.h` | Task 3 |
| R11 StateTests.cmake link fix | Task 5 Step 3b |
| R12 `Delegation7702Frame.h` | Task 2 |
| §4.2 7702 DELEGATECALL test | Task 7 test 4 |
| §8.3 smoke ctest names | Task 8 Step 4 |
| RR4 Nested ignores `gasRefund` | Task 5 (`EthHost::call` returns `fr.result` only) |
| RR8 TopLevel route parity deferred | Task 6 uses simplified route; full parity is PR2 |

**Placeholder scan:** none — all steps name files, commands, and signatures.

**Type consistency:** `RoutedMessage` used consistently; `FrameScope` defined once in `FrameScope.h`.

---

## PR1 Done Checklist

- [ ] `EthHost::call` delegates to `runExecutionFrame(Nested)`
- [ ] `routeCall` / `resolveExecutionCode` / `transferValue` / `resolveCallerAddress` removed from `EthHost`
- [ ] `StateTests.cmake` 6 vendored-EthHost targets link `bcos-evm-eth`
- [ ] `executeMessage.cpp` unchanged (still has direct `dispatchPrecompile`)
- [ ] `ExecutionFrameTest` + `PrecompileRouterEnvelope` + cross `PrecompileRouter*` green
- [ ] `eth/execution/` seam audit clean
- [ ] Ready for PR2 plan (`executeMessage` thin adapter)
