# EVM Execution Trace — Phase 0 (Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the switchable, zero-overhead-when-off trace foundation (`TraceGate` + `TracePolicy` + `TraceCollector` + `EvmCallTreeTracer` + `ExecutionTraceSession`) and wire it into `applyEthMessage`, so a nested EVM call tree can be captured on demand and nothing is attached when trace is off.

**Architecture:** Approach C (hybrid) foundation only. An `evmone::Tracer` (`EvmCallTreeTracer`) captures VM-executed frames; bcos free-function hooks in `EvmCallFrame.cpp` capture the precompile/short-circuit frames evmone never sees; both feed one `TraceCollector`. A per-transaction RAII `ExecutionTraceSession` reads a thread-local effective `TracePolicy`, attaches/detaches the evmone tracer on the real `input.vm`, and installs the thread-local collector so hook sites are armed together.

**Tech Stack:** C++20, evmone 0.21 (`evmone::Tracer`, `evmone::VM`), EVMC, Boost.Test (per-executable, included header), CMake 3.28. Source files under `bcos-evm/eth/**/*.cpp` are auto-globbed into `bcos-evm-eth` (no CMake edit needed to add library sources).

## Global Constraints

- **Switchable, no overhead when off (P1):** when the effective policy is off, `ExecutionTraceSession` is inactive, **no** evmone tracer is attached to `input.vm`, and `TraceGate::collector()` returns `nullptr`. Phase 0 uses runtime gating (one thread-local pointer load + branch per hook site). The `EVM_EXECUTION_TRACE` compile-strip (Tier B, zero-instruction) is **Phase 3**, out of scope here.
- **Reusable by debug_transaction (P2):** all new code lives in `bcos-evm/eth/trace/` and links `bcos-evm-eth` only — **no** dependency on `test/eth-eest-test`. No RPC / replay / structLogger in Phase 0.
- **Per-tx evmone tracer lifecycle:** the session MUST `remove_tracers()` on the `evmone::VM` at both activation and teardown — no tracer may remain attached after a tx completes (fixes the existing `EvmOpcodeProbe` attach-once leakage). The instrumented VM is `input.vm` (the injected `evmc::VM*`), not the `VMInstance` thread-local VM.
- **JSON / deps:** no new third-party dependency in Phase 0 (no nlohmann, no boost::property_tree yet — serialization is Phase 2).
- **Namespace:** all new types live in `namespace bcos::evm::trace`.
- **No double session:** `InnerExecute.cpp:265-268` already creates a kernel-route `EvmTraceScope` when no traceId is active; `ExecutionTraceSession` is orthogonal (it manages the collector + evmone tracer, not the `EVM_LOG` traceId) and is created only in `applyEthMessage`. Do not add a second session in `InnerExecute`.
- **Test style:** each test is its own Boost.Test executable (`#define BOOST_TEST_MODULE <Name>` + `#include <boost/test/included/unit_test.hpp>`), registered in `bcos-evm/test/cmake/TraceTests.cmake`, linked against `bcos-evm` (integration) or `bcos-evm-eth` + `evmone::evmone` (unit).

---

## File Structure (Phase 0)

New files under `bcos-evm/eth/trace/` (auto-globbed into `bcos-evm-eth`):

| File | Responsibility |
|------|----------------|
| `TraceTypes.h` | `TraceLevel`, `CallFrameEvent`, `ExecutionSummary` (Phase 0 subset; `GasLedger`/`HostEvent` are Phase 1) |
| `TraceLimits.h` | `TraceLimits` size caps |
| `TracePolicy.h` / `.cpp` | `TracePolicy` struct + `loadFromEnv()`; `current()` **declared here, defined in `TraceGate.cpp`** |
| `TraceGate.h` / `.cpp` | thread-local override stack; `enabled()`, `collector()`, `effective()`, `setGlobal()`; `TracePolicyOverride` RAII; defines `TracePolicy::current()` |
| `ITraceSink.h` | `ITraceSink` interface (`onCallFrame`, `onSummary`) |
| `TraceCollector.h` / `.cpp` | buffers `CallFrameEvent`s + `ExecutionSummary`, assigns frame ids, enforces `maxFrames`, forwards to sinks on `flush()` |
| `EvmCallTreeTracer.h` / `.cpp` | `evmone::Tracer` → `CallFrameEvent` (VM-visible frames, `fromEvmone=true`) |
| `ExecutionTraceSession.h` / `.cpp` | per-tx RAII: reads effective policy, attaches/detaches evmone tracer on `input.vm`, installs thread-local collector, emits summary on teardown |
| `FastPathFrameHook.h` | inline helper `recordFastPathFrame(...)` for bcos leaf-frame hooks (precompile / transfer-fail) |

Modified files:

| File | Change |
|------|--------|
| `bcos-evm/eth/apply/ApplyEthMessage.cpp` | construct `ExecutionTraceSession`; emit `ExecutionSummary` at buyGas-reject and at end |
| `bcos-evm/eth/kernel/execution/EvmCallFrame.cpp` | `recordFastPathFrame` calls in `runCallTargetFastPath` + `transferOrFail` |
| `bcos-evm/test/CMakeLists.txt` | `include(.../TraceTests.cmake)` |
| `bcos-evm/test/cmake/TraceTests.cmake` | new — registers all Phase 0 test executables |

**Reconciliation with spec:** Spec §12 Phase 0 item 2 says "remove `EvmOpcodeProbe` attach call site", but §11 assigns that removal to **Phase 1** (the spec is internally inconsistent here; the companion spec fix aligns §12 to §11). This plan follows §11: the `EvmOpcodeProbe::attachIfNeeded` call in `InnerExecute.cpp` is **left in place** in Phase 0. When the session is active it already `remove_tracers()` + `add_tracer()` on `input.vm`, and the probe's own attach-once guard then sees a tracer present and no-ops — so no conflict on the session-managed path.

**Scope of the leak fix (important):** Phase 0's per-tx `remove_tracers()` discipline fixes leakage **only on the `ExecutionTraceSession`-managed path** (`input.vm` when a trace session is active). It does **not** fix the pre-existing `EvmOpcodeProbe` attach-once leak that occurs under `EEST_OPCODE_TRACE=1` **with trace off**: that probe still attaches (at `InnerExecute.cpp` and `VMInstance.cpp`) and is never detached. Removing both probe attach sites is **Phase 1** (§11). The Phase 0 DoD's "no cross-tx leakage" claim is therefore scoped to the session path only.

---

## Task 1: Core trace data types + TracePolicy

**Files:**
- Create: `bcos-evm/eth/trace/TraceTypes.h`
- Create: `bcos-evm/eth/trace/TraceLimits.h`
- Create: `bcos-evm/eth/trace/TracePolicy.h`
- Create: `bcos-evm/eth/trace/TracePolicy.cpp`
- Create: `bcos-evm/test/cmake/TraceTests.cmake`
- Modify: `bcos-evm/test/CMakeLists.txt`
- Test: `bcos-evm/test/eth/TracePolicyTest.cpp`

**Interfaces:**
- Consumes: `StateTransitionExitKind` from `bcos-evm/eth/kernel/state-transition/StateTransitionContext.h`; `bcos::h256`, `bcos::bytes` from `bcos-utilities/Common.h`; `evmc_*` from `<evmc/evmc.hpp>`.
- Produces:
  - `enum class bcos::evm::trace::TraceLevel : uint8_t { Off=0, Summary=1, CallTree=2, HostEvents=3, StructLog=4 };`
  - `struct bcos::evm::trace::CallFrameEvent { uint32_t frameId; uint32_t parentFrameId; int depth; evmc_call_kind kind; evmc_address from; evmc_address to; evmc_address codeAddress; int64_t gasIn; int64_t gasOut; int64_t gasUsed; evmc_status_code status; std::string_view exitStep; bool fromEvmone; evmc_uint256be value; std::optional<evmc_address> createdAddress; };`
  - `struct bcos::evm::trace::ExecutionSummary { evmc_status_code status; StateTransitionExitKind exitKind; int64_t gasLimit; int64_t gasUsed; int64_t gasRefund; bool reachedEvmEntry; uint32_t callFrameCount; uint32_t maxDepth; std::optional<uint32_t> firstFailureFrameId; std::string diagnosis; bool truncated; };`
  - `struct bcos::evm::trace::TraceLimits { size_t maxFrames{256}; size_t maxHostEvents{4096}; size_t maxStructLogEntries{0}; size_t maxRingSummaries{1000}; };`
  - `struct bcos::evm::trace::TracePolicy { bool enabled{false}; TraceLevel level{TraceLevel::Off}; bool captureOnError{false}; TraceLevel errorLevel{TraceLevel::CallTree}; std::string captureOnErrorTracer{"callTracer"}; double sampleRate{0.0}; std::vector<bcos::h256> allowlist; TraceLimits limits; static TracePolicy loadFromEnv(); };`
  - Note: `static TracePolicy const& current() noexcept;` is **declared in Task 2** (defined in `TraceGate.cpp`), not here.

- [ ] **Step 1: Write the failing test**

Create `bcos-evm/test/eth/TracePolicyTest.cpp`:

```cpp
#define BOOST_TEST_MODULE TracePolicyTest
#include "bcos-evm/eth/trace/TracePolicy.h"
#include "bcos-evm/eth/trace/TraceTypes.h"
#include <boost/test/included/unit_test.hpp>
#include <cstdlib>

namespace bcos::evm::trace::test
{
BOOST_AUTO_TEST_SUITE(TracePolicyTest)

BOOST_AUTO_TEST_CASE(defaults_are_off)
{
    TracePolicy policy;
    BOOST_CHECK(!policy.enabled);
    BOOST_CHECK(policy.level == TraceLevel::Off);
    BOOST_CHECK(!policy.captureOnError);
    BOOST_CHECK_EQUAL(policy.limits.maxFrames, 256u);
}

BOOST_AUTO_TEST_CASE(env_enables_and_sets_level)
{
    ::setenv("EVM_TRACE_ENABLED", "1", 1);
    ::setenv("EVM_TRACE_LEVEL", "2", 1);
    auto const policy = TracePolicy::loadFromEnv();
    BOOST_CHECK(policy.enabled);
    BOOST_CHECK(policy.level == TraceLevel::CallTree);
    ::unsetenv("EVM_TRACE_ENABLED");
    ::unsetenv("EVM_TRACE_LEVEL");
}

BOOST_AUTO_TEST_CASE(env_absent_stays_off)
{
    ::unsetenv("EVM_TRACE_ENABLED");
    auto const policy = TracePolicy::loadFromEnv();
    BOOST_CHECK(!policy.enabled);
    BOOST_CHECK(policy.level == TraceLevel::Off);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::trace::test
```

- [ ] **Step 2: Run test to verify it fails**

The build fails first because the headers don't exist yet. Configure + build:

Run: `cmake --build <build-dir> --target TracePolicyTest`
Expected: FAIL — `fatal error: bcos-evm/eth/trace/TracePolicy.h: No such file or directory`

- [ ] **Step 3: Create `TraceLimits.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Hard size caps for one trace session (bounded memory).
 * @file TraceLimits.h
 */
#pragma once
#include <cstddef>

namespace bcos::evm::trace
{
struct TraceLimits
{
    size_t maxFrames{256};
    size_t maxHostEvents{4096};
    size_t maxStructLogEntries{0};
    size_t maxRingSummaries{1000};
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 4: Create `TraceTypes.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Core trace event value types (Phase 0 subset).
 * @file TraceTypes.h
 */
#pragma once

#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::evm::trace
{
enum class TraceLevel : uint8_t
{
    Off = 0,
    Summary = 1,
    CallTree = 2,
    HostEvents = 3,
    StructLog = 4,
};

/// One CALL/CREATE frame. Emitted by EvmCallTreeTracer (fromEvmone=true) or bcos hooks (false).
struct CallFrameEvent
{
    uint32_t frameId{0};
    uint32_t parentFrameId{0};
    int depth{0};
    evmc_call_kind kind{EVMC_CALL};
    evmc_address from{};
    evmc_address to{};         // for CREATE/CREATE2: created address (geth callTracer parity)
    evmc_address codeAddress{};
    int64_t gasIn{0};
    int64_t gasOut{0};
    int64_t gasUsed{0};
    evmc_status_code status{EVMC_SUCCESS};
    std::string_view exitStep;  // "runVm" | "fastPath" | "transferOrFail"
    bool fromEvmone{false};
    evmc_uint256be value{};
    std::optional<evmc_address> createdAddress;
};

/// Per-tx roll-up. GasLedger + diagnosis chain fields are populated in Phase 1.
struct ExecutionSummary
{
    evmc_status_code status{EVMC_SUCCESS};
    StateTransitionExitKind exitKind{StateTransitionExitKind::None};
    int64_t gasLimit{0};
    int64_t gasUsed{0};
    int64_t gasRefund{0};
    bool reachedEvmEntry{false};
    uint32_t callFrameCount{0};
    uint32_t maxDepth{0};
    std::optional<uint32_t> firstFailureFrameId;
    std::string diagnosis;
    bool truncated{false};
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 5: Create `TracePolicy.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Trace activation policy + env loader. current() is defined in TraceGate.cpp.
 * @file TracePolicy.h
 */
#pragma once

#include "bcos-evm/eth/trace/TraceLimits.h"
#include "bcos-evm/eth/trace/TraceTypes.h"
#include <bcos-utilities/Common.h>
#include <string>
#include <vector>

namespace bcos::evm::trace
{
struct TracePolicy
{
    bool enabled{false};
    TraceLevel level{TraceLevel::Off};
    bool captureOnError{false};
    TraceLevel errorLevel{TraceLevel::CallTree};
    std::string captureOnErrorTracer{"callTracer"};
    double sampleRate{0.0};
    std::vector<bcos::h256> allowlist;
    TraceLimits limits;

    /// Reads EVM_TRACE_ENABLED / EVM_TRACE_LEVEL / EVM_TRACE_CAPTURE_ON_ERROR.
    static TracePolicy loadFromEnv();

    /// thread_local effective policy (override top, else process global). Defined in TraceGate.cpp.
    static TracePolicy const& current() noexcept;
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 6: Create `TracePolicy.cpp`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @file TracePolicy.cpp
 */
#include "bcos-evm/eth/trace/TracePolicy.h"
#include <cstdlib>
#include <string_view>

namespace bcos::evm::trace
{
namespace
{
bool envFlag(char const* name) noexcept
{
    char const* value = std::getenv(name);
    return value != nullptr && std::string_view(value) == "1";
}
}  // namespace

TracePolicy TracePolicy::loadFromEnv()
{
    TracePolicy policy;
    policy.enabled = envFlag("EVM_TRACE_ENABLED");
    policy.captureOnError = envFlag("EVM_TRACE_CAPTURE_ON_ERROR");
    if (char const* level = std::getenv("EVM_TRACE_LEVEL"); level != nullptr)
    {
        int const parsed = std::atoi(level);
        if (parsed >= static_cast<int>(TraceLevel::Off) &&
            parsed <= static_cast<int>(TraceLevel::StructLog))
        {
            policy.level = static_cast<TraceLevel>(parsed);
        }
    }
    if (policy.enabled && policy.level == TraceLevel::Off)
    {
        policy.level = TraceLevel::CallTree;
    }
    return policy;
}
}  // namespace bcos::evm::trace
```

- [ ] **Step 7: Create `bcos-evm/test/cmake/TraceTests.cmake`**

```cmake
# bcos-evm trace foundation tests (Phase 0).

# Unit tests: pure trace types / policy / collector / evmone tracer.
foreach(TRACE_UNIT_TEST
    TracePolicyTest
)
    add_executable(${TRACE_UNIT_TEST} eth/${TRACE_UNIT_TEST}.cpp)
    target_include_directories(${TRACE_UNIT_TEST} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}
    )
    # bcos-evm-eth PUBLIC-propagates bcos-task/framework/protocol/utilities (bcos-evm/CMakeLists.txt:33-40),
    # so unit tests only need the library + evmone (matches EthTests.cmake style).
    target_link_libraries(${TRACE_UNIT_TEST} PRIVATE
        bcos-evm-eth
        evmone::evmone
    )
    add_test(NAME ${TRACE_UNIT_TEST} COMMAND ${TRACE_UNIT_TEST})
endforeach()
```

- [ ] **Step 8: Register the cmake file in `bcos-evm/test/CMakeLists.txt`**

Add after the existing `include(...)` lines (currently ending at line 12):

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TraceTests.cmake)
```

- [ ] **Step 9: Run the test to verify it passes**

Run: `cmake --build <build-dir> --target TracePolicyTest && ctest --test-dir <build-dir> -R TracePolicyTest --output-on-failure`
Expected: PASS — 3 cases (`defaults_are_off`, `env_enables_and_sets_level`, `env_absent_stays_off`).

- [ ] **Step 10: Commit**

```bash
rtk git add bcos-evm/eth/trace/TraceTypes.h bcos-evm/eth/trace/TraceLimits.h bcos-evm/eth/trace/TracePolicy.h bcos-evm/eth/trace/TracePolicy.cpp bcos-evm/test/cmake/TraceTests.cmake bcos-evm/test/CMakeLists.txt bcos-evm/test/eth/TracePolicyTest.cpp
rtk git commit -m "feat(evm-trace): add core trace types + TracePolicy env loader (Phase 0)"
```

---

## Task 2: TraceGate + TracePolicyOverride (thread-local effective policy)

**Files:**
- Create: `bcos-evm/eth/trace/TraceGate.h`
- Create: `bcos-evm/eth/trace/TraceGate.cpp`
- Test: `bcos-evm/test/eth/TracePolicyOverrideTest.cpp`
- Modify: `bcos-evm/test/cmake/TraceTests.cmake`

**Interfaces:**
- Consumes: `TracePolicy` (Task 1). Forward-declares `TraceCollector` (Task 3) — only a pointer is stored, so no include cycle.
- Produces:
  - `class bcos::evm::trace::TraceGate` with statics: `static bool enabled() noexcept;` `static TraceCollector* collector() noexcept;` `static TracePolicy const& effective() noexcept;` `static void setGlobal(TracePolicy policy);` and internal `static void pushOverride(TracePolicy policy, TraceCollector* collector) noexcept;` `static void popOverride() noexcept;`
  - `class bcos::evm::trace::TracePolicyOverride` (RAII): `TracePolicyOverride(TracePolicy policy, TraceCollector* collector) noexcept;` `~TracePolicyOverride() noexcept;` non-copyable.
  - Defines `TracePolicy::current()` (returns `TraceGate::effective()`).
- Semantics: `enabled()` == `effective().enabled`. `collector()` returns the override-top collector, or `nullptr` when no override is active. `effective()` returns override-top policy, else the process global. The invariant is that a session pushes `{policy, collector}` **together**, so `enabled()` and `collector()` always agree while a session is live.
- **Activation authority (resolves the enabled-vs-level split):** `ExecutionTraceSession` is the **single** decision point for activation; it activates iff `policy.enabled && policy.level > TraceLevel::Off`, and only then pushes an override. `TraceGate::enabled()` is therefore **not** used on its own anywhere to gate the evmone attach — the attach is performed by the session, which pushes the override so `enabled()`/`collector()` become consistent for the duration. This closes the degenerate `enabled=true, level=Off` case: `enabled()` may read `true`, but no session activates, so `collector()` stays `nullptr` and nothing is attached. `TracePolicy::loadFromEnv()` additionally promotes `level` to `CallTree` when `EVM_TRACE_ENABLED=1` is set without an explicit level (Task 1), so the env path never lands in that degenerate state. Phase 1 must preserve this rule if it adds any `InnerExecute`-side attach.

- [ ] **Step 1: Write the failing test**

Create `bcos-evm/test/eth/TracePolicyOverrideTest.cpp`:

```cpp
#define BOOST_TEST_MODULE TracePolicyOverrideTest
#include "bcos-evm/eth/trace/TraceGate.h"
#include "bcos-evm/eth/trace/TracePolicy.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::trace::test
{
BOOST_AUTO_TEST_SUITE(TracePolicyOverrideTest)

BOOST_AUTO_TEST_CASE(no_override_is_off_and_no_collector)
{
    BOOST_CHECK(!TraceGate::enabled());
    BOOST_CHECK(TraceGate::collector() == nullptr);
}

BOOST_AUTO_TEST_CASE(override_enables_then_restores)
{
    TracePolicy policy;
    policy.enabled = true;
    policy.level = TraceLevel::CallTree;

    // Use a dummy non-null collector pointer to check collector() plumbing.
    auto* dummy = reinterpret_cast<TraceCollector*>(0x1);
    {
        TracePolicyOverride override(policy, dummy);
        BOOST_CHECK(TraceGate::enabled());
        BOOST_CHECK(TraceGate::collector() == dummy);
        BOOST_CHECK(TraceGate::effective().level == TraceLevel::CallTree);
        BOOST_CHECK(TracePolicy::current().enabled);
    }
    BOOST_CHECK(!TraceGate::enabled());
    BOOST_CHECK(TraceGate::collector() == nullptr);
}

BOOST_AUTO_TEST_CASE(overrides_nest_lifo)
{
    TracePolicy outer;
    outer.enabled = true;
    outer.level = TraceLevel::Summary;
    TracePolicy inner;
    inner.enabled = true;
    inner.level = TraceLevel::HostEvents;

    TracePolicyOverride o1(outer, reinterpret_cast<TraceCollector*>(0x1));
    BOOST_CHECK(TraceGate::effective().level == TraceLevel::Summary);
    {
        TracePolicyOverride o2(inner, reinterpret_cast<TraceCollector*>(0x2));
        BOOST_CHECK(TraceGate::effective().level == TraceLevel::HostEvents);
        BOOST_CHECK(TraceGate::collector() == reinterpret_cast<TraceCollector*>(0x2));
    }
    BOOST_CHECK(TraceGate::effective().level == TraceLevel::Summary);
    BOOST_CHECK(TraceGate::collector() == reinterpret_cast<TraceCollector*>(0x1));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::trace::test
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build <build-dir> --target TracePolicyOverrideTest`
Expected: FAIL — `fatal error: bcos-evm/eth/trace/TraceGate.h: No such file or directory`

- [ ] **Step 3: Create `TraceGate.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Thread-local trace activation gate + RAII policy override.
 * @file TraceGate.h
 *
 * TraceGate reads a thread-local effective policy so consensus, EEST, and (Phase 2) RPC
 * replay differ per thread and per tx without a process-wide bool. A session pushes
 * {policy, collector} together via TracePolicyOverride so enabled() and collector() agree.
 */
#pragma once

#include "bcos-evm/eth/trace/TracePolicy.h"

namespace bcos::evm::trace
{
class TraceCollector;

class TraceGate
{
public:
    static bool enabled() noexcept;
    static TraceCollector* collector() noexcept;
    static TracePolicy const& effective() noexcept;

    /// Set process-wide default (loaded once at startup). Reloadable.
    static void setGlobal(TracePolicy policy);

    /// Internal — used by TracePolicyOverride / ExecutionTraceSession only.
    static void pushOverride(TracePolicy policy, TraceCollector* collector) noexcept;
    static void popOverride() noexcept;
};

/// Installs {policy, collector} on the thread-local stack for one scope; pops on destruction.
class TracePolicyOverride
{
public:
    TracePolicyOverride(TracePolicy policy, TraceCollector* collector) noexcept;
    ~TracePolicyOverride() noexcept;

    TracePolicyOverride(TracePolicyOverride const&) = delete;
    TracePolicyOverride& operator=(TracePolicyOverride const&) = delete;
    TracePolicyOverride(TracePolicyOverride&&) = delete;
    TracePolicyOverride& operator=(TracePolicyOverride&&) = delete;
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 4: Create `TraceGate.cpp`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @file TraceGate.cpp
 */
#include "bcos-evm/eth/trace/TraceGate.h"
#include <vector>

namespace bcos::evm::trace
{
namespace
{
struct Entry
{
    TracePolicy policy;
    TraceCollector* collector;
};

TracePolicy& globalPolicy()
{
    static TracePolicy policy;
    return policy;
}

thread_local std::vector<Entry> t_overrides;
}  // namespace

void TraceGate::setGlobal(TracePolicy policy)
{
    globalPolicy() = std::move(policy);
}

void TraceGate::pushOverride(TracePolicy policy, TraceCollector* collector) noexcept
{
    t_overrides.push_back(Entry{std::move(policy), collector});
}

void TraceGate::popOverride() noexcept
{
    if (!t_overrides.empty())
    {
        t_overrides.pop_back();
    }
}

TracePolicy const& TraceGate::effective() noexcept
{
    if (!t_overrides.empty())
    {
        return t_overrides.back().policy;
    }
    return globalPolicy();
}

bool TraceGate::enabled() noexcept
{
    return effective().enabled;
}

TraceCollector* TraceGate::collector() noexcept
{
    if (!t_overrides.empty())
    {
        return t_overrides.back().collector;
    }
    return nullptr;
}

TracePolicyOverride::TracePolicyOverride(TracePolicy policy, TraceCollector* collector) noexcept
{
    TraceGate::pushOverride(std::move(policy), collector);
}

TracePolicyOverride::~TracePolicyOverride() noexcept
{
    TraceGate::popOverride();
}

TracePolicy const& TracePolicy::current() noexcept
{
    return TraceGate::effective();
}
}  // namespace bcos::evm::trace
```

- [ ] **Step 5: Add the test to `TraceTests.cmake`**

Add `TracePolicyOverrideTest` to the `foreach(TRACE_UNIT_TEST ...)` list:

```cmake
foreach(TRACE_UNIT_TEST
    TracePolicyTest
    TracePolicyOverrideTest
)
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build <build-dir> --target TracePolicyOverrideTest && ctest --test-dir <build-dir> -R TracePolicyOverrideTest --output-on-failure`
Expected: PASS — 3 cases.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/trace/TraceGate.h bcos-evm/eth/trace/TraceGate.cpp bcos-evm/test/eth/TracePolicyOverrideTest.cpp bcos-evm/test/cmake/TraceTests.cmake
rtk git commit -m "feat(evm-trace): add TraceGate thread-local effective policy + RAII override (Phase 0)"
```

---

## Task 3: ITraceSink + TraceCollector

**Files:**
- Create: `bcos-evm/eth/trace/ITraceSink.h`
- Create: `bcos-evm/eth/trace/TraceCollector.h`
- Create: `bcos-evm/eth/trace/TraceCollector.cpp`
- Test: `bcos-evm/test/eth/TraceCollectorTest.cpp`
- Modify: `bcos-evm/test/cmake/TraceTests.cmake`

**Interfaces:**
- Consumes: `CallFrameEvent`, `ExecutionSummary` (Task 1), `TraceLimits` (Task 1).
- Produces:
  - `struct bcos::evm::trace::ITraceSink { virtual ~ITraceSink() = default; virtual void onCallFrame(CallFrameEvent const&) {} virtual void onSummary(ExecutionSummary const&) {} };`
  - `class bcos::evm::trace::TraceCollector` with:
    - `explicit TraceCollector(TraceLimits limits) noexcept;`
    - `void addSink(std::shared_ptr<ITraceSink> sink);`
    - `uint32_t nextFrameId() noexcept;` — monotonic from 1
    - `void onCallFrame(CallFrameEvent event);` — buffers; if `frames().size() >= maxFrames`, drops + sets `truncated()`
    - `void onSummary(ExecutionSummary summary);` — stores (overwrites)
    - `void flush();` — forwards buffered frames (in order) then summary to every sink
    - accessors: `std::vector<CallFrameEvent> const& frames() const noexcept;` `ExecutionSummary const& summary() const noexcept;` `bool hasSummary() const noexcept;` `bool truncated() const noexcept;`

- [ ] **Step 1: Write the failing test**

Create `bcos-evm/test/eth/TraceCollectorTest.cpp`:

```cpp
#define BOOST_TEST_MODULE TraceCollectorTest
#include "bcos-evm/eth/trace/ITraceSink.h"
#include "bcos-evm/eth/trace/TraceCollector.h"
#include <boost/test/included/unit_test.hpp>
#include <memory>

namespace bcos::evm::trace::test
{
namespace
{
struct CountingSink : ITraceSink
{
    int frames{0};
    int summaries{0};
    void onCallFrame(CallFrameEvent const&) override { ++frames; }
    void onSummary(ExecutionSummary const&) override { ++summaries; }
};

CallFrameEvent makeFrame(uint32_t id)
{
    CallFrameEvent e;
    e.frameId = id;
    return e;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TraceCollectorTest)

BOOST_AUTO_TEST_CASE(assigns_monotonic_frame_ids)
{
    TraceCollector collector(TraceLimits{});
    BOOST_CHECK_EQUAL(collector.nextFrameId(), 1u);
    BOOST_CHECK_EQUAL(collector.nextFrameId(), 2u);
    BOOST_CHECK_EQUAL(collector.nextFrameId(), 3u);
}

BOOST_AUTO_TEST_CASE(buffers_and_flushes_to_sink)
{
    TraceCollector collector(TraceLimits{});
    auto sink = std::make_shared<CountingSink>();
    collector.addSink(sink);

    collector.onCallFrame(makeFrame(1));
    collector.onCallFrame(makeFrame(2));
    ExecutionSummary summary;
    summary.gasUsed = 42;
    collector.onSummary(summary);

    BOOST_CHECK_EQUAL(collector.frames().size(), 2u);
    BOOST_CHECK(collector.hasSummary());
    BOOST_CHECK_EQUAL(collector.summary().gasUsed, 42);
    BOOST_CHECK_EQUAL(sink->frames, 0);  // not forwarded until flush

    collector.flush();
    BOOST_CHECK_EQUAL(sink->frames, 2);
    BOOST_CHECK_EQUAL(sink->summaries, 1);
}

BOOST_AUTO_TEST_CASE(truncates_at_max_frames)
{
    TraceLimits limits;
    limits.maxFrames = 2;
    TraceCollector collector(limits);
    collector.onCallFrame(makeFrame(1));
    collector.onCallFrame(makeFrame(2));
    collector.onCallFrame(makeFrame(3));  // dropped
    BOOST_CHECK_EQUAL(collector.frames().size(), 2u);
    BOOST_CHECK(collector.truncated());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::trace::test
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build <build-dir> --target TraceCollectorTest`
Expected: FAIL — `fatal error: bcos-evm/eth/trace/TraceCollector.h: No such file or directory`

- [ ] **Step 3: Create `ITraceSink.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Pluggable trace output interface. Phase 1 adds LogTraceSink / RingBufferSink.
 * @file ITraceSink.h
 */
#pragma once

#include "bcos-evm/eth/trace/TraceTypes.h"

namespace bcos::evm::trace
{
struct ITraceSink
{
    virtual ~ITraceSink() = default;
    virtual void onCallFrame(CallFrameEvent const& /*event*/) {}
    virtual void onSummary(ExecutionSummary const& /*summary*/) {}
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 4: Create `TraceCollector.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Aggregates one tx's trace events; buffers then forwards to sinks on flush().
 * @file TraceCollector.h
 */
#pragma once

#include "bcos-evm/eth/trace/ITraceSink.h"
#include "bcos-evm/eth/trace/TraceLimits.h"
#include "bcos-evm/eth/trace/TraceTypes.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace bcos::evm::trace
{
class TraceCollector
{
public:
    explicit TraceCollector(TraceLimits limits) noexcept : m_limits(limits) {}

    void addSink(std::shared_ptr<ITraceSink> sink);
    uint32_t nextFrameId() noexcept;
    void onCallFrame(CallFrameEvent event);
    void onSummary(ExecutionSummary summary);
    void flush();

    std::vector<CallFrameEvent> const& frames() const noexcept { return m_frames; }
    ExecutionSummary const& summary() const noexcept { return m_summary; }
    bool hasSummary() const noexcept { return m_hasSummary; }
    bool truncated() const noexcept { return m_truncated; }

private:
    TraceLimits m_limits;
    std::vector<std::shared_ptr<ITraceSink>> m_sinks;
    std::vector<CallFrameEvent> m_frames;
    ExecutionSummary m_summary;
    uint32_t m_frameSeq{0};
    bool m_hasSummary{false};
    bool m_truncated{false};
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 5: Create `TraceCollector.cpp`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @file TraceCollector.cpp
 */
#include "bcos-evm/eth/trace/TraceCollector.h"
#include <utility>

namespace bcos::evm::trace
{
void TraceCollector::addSink(std::shared_ptr<ITraceSink> sink)
{
    if (sink != nullptr)
    {
        m_sinks.push_back(std::move(sink));
    }
}

uint32_t TraceCollector::nextFrameId() noexcept
{
    return ++m_frameSeq;
}

void TraceCollector::onCallFrame(CallFrameEvent event)
{
    if (m_frames.size() >= m_limits.maxFrames)
    {
        m_truncated = true;
        return;
    }
    m_frames.push_back(std::move(event));
}

void TraceCollector::onSummary(ExecutionSummary summary)
{
    summary.callFrameCount = static_cast<uint32_t>(m_frames.size());
    summary.truncated = summary.truncated || m_truncated;
    m_summary = std::move(summary);
    m_hasSummary = true;
}

void TraceCollector::flush()
{
    for (auto const& sink : m_sinks)
    {
        for (auto const& frame : m_frames)
        {
            sink->onCallFrame(frame);
        }
        if (m_hasSummary)
        {
            sink->onSummary(m_summary);
        }
    }
}
}  // namespace bcos::evm::trace
```

- [ ] **Step 6: Add the test to `TraceTests.cmake`**

Add `TraceCollectorTest` to the `foreach(TRACE_UNIT_TEST ...)` list:

```cmake
foreach(TRACE_UNIT_TEST
    TracePolicyTest
    TracePolicyOverrideTest
    TraceCollectorTest
)
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `cmake --build <build-dir> --target TraceCollectorTest && ctest --test-dir <build-dir> -R TraceCollectorTest --output-on-failure`
Expected: PASS — 3 cases.

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/eth/trace/ITraceSink.h bcos-evm/eth/trace/TraceCollector.h bcos-evm/eth/trace/TraceCollector.cpp bcos-evm/test/eth/TraceCollectorTest.cpp bcos-evm/test/cmake/TraceTests.cmake
rtk git commit -m "feat(evm-trace): add ITraceSink + buffering TraceCollector (Phase 0)"
```

---

## Task 4: EvmCallTreeTracer (evmone::Tracer → CallFrameEvent)

**Files:**
- Create: `bcos-evm/eth/trace/EvmCallTreeTracer.h`
- Create: `bcos-evm/eth/trace/EvmCallTreeTracer.cpp`
- Test: `bcos-evm/test/eth/EvmCallTreeTracerTest.cpp`
- Modify: `bcos-evm/test/cmake/TraceTests.cmake`

**Interfaces:**
- Consumes: `evmone::Tracer` base (`<evmone/tracing.hpp>`), `TraceCollector` (Task 3), `CallFrameEvent` (Task 1). The evmone hook signatures are verified against `EvmOpcodeProbe.h`: `on_execution_start(evmc_revision, const evmc_message&, evmc::bytes_view)`, `on_instruction_start(uint32_t, const intx::uint256*, int, int64_t, const evmone::ExecutionState&)`, `on_execution_end(const evmc_result&)`.
- Produces:
  - `class bcos::evm::trace::EvmCallTreeTracer final : public evmone::Tracer` with ctor `explicit EvmCallTreeTracer(TraceCollector& collector) noexcept;`
- Behavior: maintains an internal stack; `on_execution_start` assigns `frameId = collector.nextFrameId()`, `parentFrameId = stack empty ? 0 : stack.back().frameId`, records enter data (depth, kind, from=`msg.sender`, to=`msg.recipient`, codeAddress=`msg.code_address`, gasIn=`msg.gas`, value). `on_execution_end` pops, fills `gasOut=result.gas_left`, `gasUsed=gasIn-gasOut`, `status`, and for CREATE/CREATE2 sets `createdAddress = result.create_address` and `to = result.create_address`; emits `CallFrameEvent` with `fromEvmone=true`.

- [ ] **Step 1: Write the failing test**

The tracer's evmone hooks are `protected`/`private` overrides, so the test exercises them by driving a real evmone VM over trivial bytecode. Create `bcos-evm/test/eth/EvmCallTreeTracerTest.cpp`:

```cpp
#define BOOST_TEST_MODULE EvmCallTreeTracerTest
#include "bcos-evm/eth/trace/EvmCallTreeTracer.h"
#include "bcos-evm/eth/trace/TraceCollector.h"
#include <evmone/evmone.h>
#include <evmone/vm.hpp>
#include <evmc/evmc.hpp>
#include <boost/test/included/unit_test.hpp>
#include <memory>

namespace bcos::evm::trace::test
{
BOOST_AUTO_TEST_SUITE(EvmCallTreeTracerTest)

// STOP (0x00): a single top-level frame, no nesting, immediate success.
BOOST_AUTO_TEST_CASE(records_single_top_level_frame)
{
    TraceCollector collector(TraceLimits{});

    evmc::VM vm{evmc_create_evmone()};
    auto* evmoneVm = static_cast<evmone::VM*>(vm.get_raw_pointer());
    evmoneVm->add_tracer(std::make_unique<EvmCallTreeTracer>(collector));

    uint8_t const code[] = {0x00};  // STOP
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 100000;

    // Host-less four-arg overload: STOP never invokes a host callback.
    auto const result = vm.execute(EVMC_SHANGHAI, msg, code, sizeof(code));
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

    BOOST_REQUIRE_EQUAL(collector.frames().size(), 1u);
    auto const& frame = collector.frames().front();
    BOOST_CHECK_EQUAL(frame.frameId, 1u);
    BOOST_CHECK_EQUAL(frame.parentFrameId, 0u);
    BOOST_CHECK_EQUAL(frame.depth, 0);
    BOOST_CHECK_EQUAL(frame.gasIn, 100000);
    BOOST_CHECK(frame.status == EVMC_SUCCESS);
    BOOST_CHECK(frame.fromEvmone);
    BOOST_CHECK(frame.gasOut <= frame.gasIn);
    BOOST_CHECK_EQUAL(frame.gasUsed, frame.gasIn - frame.gasOut);

    evmoneVm->remove_tracers();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::trace::test
```

> **Note for implementer:** the host-less four-arg `evmc::VM::execute(evmc_revision, const evmc_message&, const uint8_t*, size_t)` overload (verified at `evmc.hpp:759-766`) runs STOP without any host callback. Do **not** use `evmc::VM::null_host_context()` — it does not exist in this EVMC version.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build <build-dir> --target EvmCallTreeTracerTest`
Expected: FAIL — `fatal error: bcos-evm/eth/trace/EvmCallTreeTracer.h: No such file or directory`

- [ ] **Step 3: Create `EvmCallTreeTracer.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief evmone::Tracer that turns VM-executed frames into CallFrameEvent (fromEvmone=true).
 * @file EvmCallTreeTracer.h
 */
#pragma once

#include "bcos-evm/eth/trace/TraceTypes.h"
#include <evmone/tracing.hpp>
#include <evmc/evmc.hpp>
#include <cstdint>
#include <vector>

namespace bcos::evm::trace
{
class TraceCollector;

class EvmCallTreeTracer final : public evmone::Tracer
{
public:
    explicit EvmCallTreeTracer(TraceCollector& collector) noexcept : m_collector(collector) {}

private:
    void on_execution_start(
        evmc_revision rev, const evmc_message& msg, evmc::bytes_view code) noexcept override;
    void on_instruction_start(uint32_t pc, const intx::uint256* stack_top, int stack_height,
        int64_t gas, const evmone::ExecutionState& state) noexcept override;
    void on_execution_end(const evmc_result& result) noexcept override;

    struct Pending
    {
        uint32_t frameId;
        uint32_t parentFrameId;
        int depth;
        evmc_call_kind kind;
        evmc_address from;
        evmc_address to;
        evmc_address codeAddress;
        int64_t gasIn;
        evmc_uint256be value;
    };

    TraceCollector& m_collector;
    std::vector<Pending> m_stack;
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 4: Create `EvmCallTreeTracer.cpp`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @file EvmCallTreeTracer.cpp
 */
#include "bcos-evm/eth/trace/EvmCallTreeTracer.h"
#include "bcos-evm/eth/trace/TraceCollector.h"

namespace bcos::evm::trace
{
void EvmCallTreeTracer::on_execution_start(
    evmc_revision /*rev*/, const evmc_message& msg, evmc::bytes_view /*code*/) noexcept
{
    Pending pending{};
    pending.frameId = m_collector.nextFrameId();
    pending.parentFrameId = m_stack.empty() ? 0U : m_stack.back().frameId;
    pending.depth = msg.depth;
    pending.kind = msg.kind;
    pending.from = msg.sender;
    pending.to = msg.recipient;
    pending.codeAddress = msg.code_address;
    pending.gasIn = msg.gas;
    pending.value = msg.value;
    m_stack.push_back(pending);
}

void EvmCallTreeTracer::on_instruction_start(uint32_t /*pc*/, const intx::uint256* /*stack_top*/,
    int /*stack_height*/, int64_t /*gas*/, const evmone::ExecutionState& /*state*/) noexcept
{
    // StructLog (per-opcode) is Phase 2; no work here in Phase 0.
}

void EvmCallTreeTracer::on_execution_end(const evmc_result& result) noexcept
{
    if (m_stack.empty())
    {
        return;
    }
    Pending const pending = m_stack.back();
    m_stack.pop_back();

    CallFrameEvent event;
    event.frameId = pending.frameId;
    event.parentFrameId = pending.parentFrameId;
    event.depth = pending.depth;
    event.kind = pending.kind;
    event.from = pending.from;
    event.to = pending.to;
    event.codeAddress = pending.codeAddress;
    event.gasIn = pending.gasIn;
    event.gasOut = result.gas_left;
    event.gasUsed = pending.gasIn - result.gas_left;
    event.status = result.status_code;
    event.exitStep = "runVm";
    event.fromEvmone = true;
    event.value = pending.value;
    if (pending.kind == EVMC_CREATE || pending.kind == EVMC_CREATE2)
    {
        event.createdAddress = result.create_address;
        event.to = result.create_address;
    }
    m_collector.onCallFrame(event);
}
}  // namespace bcos::evm::trace
```

- [ ] **Step 5: Add the test to `TraceTests.cmake`**

`EvmCallTreeTracerTest` drives a real evmone VM, so register it separately from the pure-unit list (it needs `evmone::evmone`, which the unit list already links). Add it to the `foreach(TRACE_UNIT_TEST ...)` list:

```cmake
foreach(TRACE_UNIT_TEST
    TracePolicyTest
    TracePolicyOverrideTest
    TraceCollectorTest
    EvmCallTreeTracerTest
)
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build <build-dir> --target EvmCallTreeTracerTest && ctest --test-dir <build-dir> -R EvmCallTreeTracerTest --output-on-failure`
Expected: PASS — 1 case, one frame with `fromEvmone=true`, `gasUsed == gasIn - gasOut`.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/trace/EvmCallTreeTracer.h bcos-evm/eth/trace/EvmCallTreeTracer.cpp bcos-evm/test/eth/EvmCallTreeTracerTest.cpp bcos-evm/test/cmake/TraceTests.cmake
rtk git commit -m "feat(evm-trace): add EvmCallTreeTracer evmone bridge (Phase 0)"
```

---

## Task 5: ExecutionTraceSession (per-tx RAII attach/detach)

**Files:**
- Create: `bcos-evm/eth/trace/ExecutionTraceSession.h`
- Create: `bcos-evm/eth/trace/ExecutionTraceSession.cpp`
- Test: `bcos-evm/test/eth/ExecutionTraceSessionTest.cpp`
- Modify: `bcos-evm/test/cmake/TraceTests.cmake`

**Interfaces:**
- Consumes: `TracePolicy::current()` (Task 2), `TraceCollector` (Task 3), `EvmCallTreeTracer` (Task 4), `TracePolicyOverride` (Task 2), `EvmTraceContext` + `makeTraceContext` (existing `eth/trace/EvmTrace.h`), `evmc::VM` + `evmone::VM` (`get_raw_pointer()`).
- Produces:
  - `class bcos::evm::trace::ExecutionTraceSession` with:
    - `ExecutionTraceSession(evmc::VM& vm, EvmTraceContext ctx) noexcept;`
    - `~ExecutionTraceSession() noexcept;`
    - `bool active() const noexcept;`
    - `TraceCollector* collector() noexcept;` — non-null only when active
    - `void setSummary(ExecutionSummary summary) noexcept;` — stored; emitted at teardown
    - non-copyable, non-movable
- Behavior: constructor reads `TracePolicy::current()`; activates iff `policy.enabled && policy.level > TraceLevel::Off`. When activating: builds a `TraceCollector` from `policy.limits`, installs a `TracePolicyOverride{policy, collector}` (arms `TraceGate::collector()`), and if `policy.level >= CallTree` calls `remove_tracers()` then `add_tracer(EvmCallTreeTracer)` on the underlying `evmone::VM`. Destructor (when active): emits stored summary to the collector, `flush()`es, `remove_tracers()` on the VM, and pops the override. (Phase 0 registers no sinks; the collector is inspected directly by tests / consumed by Phase 1 sinks.)

- [ ] **Step 0: Verify the evmone tracer-removal API exists**

Run: `rtk grep -n "remove_tracers" $(dirname $(find / -name vm.hpp -path "*evmone*" 2>/dev/null | head -1))` — or grep the evmone include dir used by the build.
Expected: a declaration `void remove_tracers() noexcept;` on `evmone::VM`. If the API name differs in the pinned evmone version, adjust the two call sites in `ExecutionTraceSession.cpp` accordingly (this is the only external-API assumption in Phase 0).

- [ ] **Step 1: Write the failing test**

Create `bcos-evm/test/eth/ExecutionTraceSessionTest.cpp`:

```cpp
#define BOOST_TEST_MODULE ExecutionTraceSessionTest
#include "bcos-evm/eth/trace/ExecutionTraceSession.h"
#include "bcos-evm/eth/trace/TraceGate.h"
#include "bcos-evm/eth/trace/TracePolicy.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <evmone/evmone.h>
#include <evmone/vm.hpp>
#include <evmc/evmc.hpp>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::trace::test
{
namespace
{
struct GlobalPolicyGuard
{
    explicit GlobalPolicyGuard(TracePolicy policy) { TraceGate::setGlobal(std::move(policy)); }
    ~GlobalPolicyGuard() { TraceGate::setGlobal(TracePolicy{}); }
};

bool hasTracer(evmc::VM& vm)
{
    return static_cast<evmone::VM*>(vm.get_raw_pointer())->get_tracer() != nullptr;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ExecutionTraceSessionTest)

BOOST_AUTO_TEST_CASE(disabled_policy_attaches_nothing)
{
    evmc::VM vm{evmc_create_evmone()};
    {
        ExecutionTraceSession session(vm, makeTraceContext("eth", 1));
        BOOST_CHECK(!session.active());
        BOOST_CHECK(session.collector() == nullptr);
        BOOST_CHECK(TraceGate::collector() == nullptr);
        BOOST_CHECK(!hasTracer(vm));
    }
    BOOST_CHECK(TraceGate::collector() == nullptr);
    BOOST_CHECK(!hasTracer(vm));
}

BOOST_AUTO_TEST_CASE(enabled_policy_attaches_and_detaches)
{
    TracePolicy policy;
    policy.enabled = true;
    policy.level = TraceLevel::CallTree;
    GlobalPolicyGuard guard(policy);

    evmc::VM vm{evmc_create_evmone()};
    {
        ExecutionTraceSession session(vm, makeTraceContext("eth", 1));
        BOOST_CHECK(session.active());
        BOOST_CHECK(session.collector() != nullptr);
        BOOST_CHECK(TraceGate::collector() == session.collector());
        BOOST_CHECK(hasTracer(vm));  // EvmCallTreeTracer attached
    }
    // After teardown: detached and collector cleared (no cross-tx leakage).
    BOOST_CHECK(TraceGate::collector() == nullptr);
    BOOST_CHECK(!hasTracer(vm));
}

BOOST_AUTO_TEST_CASE(summary_only_level_does_not_attach_tracer)
{
    TracePolicy policy;
    policy.enabled = true;
    policy.level = TraceLevel::Summary;
    GlobalPolicyGuard guard(policy);

    evmc::VM vm{evmc_create_evmone()};
    {
        ExecutionTraceSession session(vm, makeTraceContext("eth", 1));
        BOOST_CHECK(session.active());        // collector armed
        BOOST_CHECK(session.collector() != nullptr);
        BOOST_CHECK(!hasTracer(vm));          // but no evmone tracer below CallTree
    }
    BOOST_CHECK(!hasTracer(vm));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::trace::test
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build <build-dir> --target ExecutionTraceSessionTest`
Expected: FAIL — `fatal error: bcos-evm/eth/trace/ExecutionTraceSession.h: No such file or directory`

- [ ] **Step 3: Create `ExecutionTraceSession.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Per-transaction RAII trace session: attaches/detaches the evmone tracer on the
 *        executing VM and installs the thread-local collector so hook sites arm together.
 * @file ExecutionTraceSession.h
 */
#pragma once

#include "bcos-evm/eth/trace/TraceGate.h"
#include "bcos-evm/eth/trace/TraceTypes.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <evmc/evmc.hpp>
#include <memory>
#include <optional>

namespace bcos::evm::trace
{
class TraceCollector;

class ExecutionTraceSession
{
public:
    ExecutionTraceSession(evmc::VM& vm, EvmTraceContext ctx) noexcept;
    ~ExecutionTraceSession() noexcept;

    ExecutionTraceSession(ExecutionTraceSession const&) = delete;
    ExecutionTraceSession& operator=(ExecutionTraceSession const&) = delete;
    ExecutionTraceSession(ExecutionTraceSession&&) = delete;
    ExecutionTraceSession& operator=(ExecutionTraceSession&&) = delete;

    bool active() const noexcept { return m_active; }
    TraceCollector* collector() noexcept;
    void setSummary(ExecutionSummary summary) noexcept;

private:
    bool m_active{false};
    evmc::VM* m_vm{nullptr};
    EvmTraceContext m_ctx;
    std::unique_ptr<TraceCollector> m_collector;
    std::optional<TracePolicyOverride> m_override;
    ExecutionSummary m_summary;
    bool m_hasSummary{false};
};
}  // namespace bcos::evm::trace
```

- [ ] **Step 4: Create `ExecutionTraceSession.cpp`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 * @file ExecutionTraceSession.cpp
 */
#include "bcos-evm/eth/trace/ExecutionTraceSession.h"
#include "bcos-evm/eth/trace/EvmCallTreeTracer.h"
#include "bcos-evm/eth/trace/TraceCollector.h"
#include "bcos-evm/eth/trace/TracePolicy.h"
#include <evmone/vm.hpp>
#include <utility>

namespace bcos::evm::trace
{
namespace
{
evmone::VM* asEvmone(evmc::VM& vm) noexcept
{
    return static_cast<evmone::VM*>(vm.get_raw_pointer());
}
}  // namespace

ExecutionTraceSession::ExecutionTraceSession(evmc::VM& vm, EvmTraceContext ctx) noexcept
  : m_vm(&vm), m_ctx(std::move(ctx))
{
    TracePolicy const policy = TracePolicy::current();
    if (!policy.enabled || policy.level <= TraceLevel::Off)
    {
        return;  // inactive: no override, no tracer, no collector — zero trace work
    }

    m_collector = std::make_unique<TraceCollector>(policy.limits);
    // Install {policy, collector} together so TraceGate::enabled() and collector() agree.
    m_override.emplace(policy, m_collector.get());

    if (policy.level >= TraceLevel::CallTree)
    {
        auto* evmoneVm = asEvmone(vm);
        evmoneVm->remove_tracers();  // per-tx lifecycle: clear any stale tracer first
        evmoneVm->add_tracer(std::make_unique<EvmCallTreeTracer>(*m_collector));
    }
    m_active = true;
}

ExecutionTraceSession::~ExecutionTraceSession() noexcept
{
    if (!m_active)
    {
        return;
    }
    if (m_hasSummary && m_collector != nullptr)
    {
        m_collector->onSummary(std::move(m_summary));
    }
    if (m_collector != nullptr)
    {
        m_collector->flush();
    }
    if (m_vm != nullptr)
    {
        asEvmone(*m_vm)->remove_tracers();  // REQUIRED — no cross-tx leakage
    }
    m_override.reset();  // pops the thread-local override → collector() back to nullptr
}

TraceCollector* ExecutionTraceSession::collector() noexcept
{
    return m_active ? m_collector.get() : nullptr;
}

void ExecutionTraceSession::setSummary(ExecutionSummary summary) noexcept
{
    m_summary = std::move(summary);
    m_hasSummary = true;
}
}  // namespace bcos::evm::trace
```

- [ ] **Step 5: Add the test to `TraceTests.cmake`**

Add `ExecutionTraceSessionTest` to the `foreach(TRACE_UNIT_TEST ...)` list:

```cmake
foreach(TRACE_UNIT_TEST
    TracePolicyTest
    TracePolicyOverrideTest
    TraceCollectorTest
    EvmCallTreeTracerTest
    ExecutionTraceSessionTest
)
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build <build-dir> --target ExecutionTraceSessionTest && ctest --test-dir <build-dir> -R ExecutionTraceSessionTest --output-on-failure`
Expected: PASS — 3 cases: disabled attaches nothing; enabled attaches then detaches; Summary-level arms the collector without attaching an evmone tracer.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/trace/ExecutionTraceSession.h bcos-evm/eth/trace/ExecutionTraceSession.cpp bcos-evm/test/eth/ExecutionTraceSessionTest.cpp bcos-evm/test/cmake/TraceTests.cmake
rtk git commit -m "feat(evm-trace): add per-tx ExecutionTraceSession attach/detach (Phase 0)"
```

---

## Task 6: Wire ExecutionTraceSession into applyEthMessage + nested call-tree integration test

**Files:**
- Modify: `bcos-evm/eth/apply/ApplyEthMessage.cpp` (add include + session + summary emission)
- Create: `bcos-evm/test/eth/TraceCallTreeTest.cpp` (integration: nested CALL over EthHost+VM)
- Modify: `bcos-evm/test/cmake/TraceTests.cmake` (add integration test target)

**Interfaces:**
- Consumes: `ExecutionTraceSession`, `TraceGate`, `ExecutionSummary`, `makeTraceContext` (Tasks 1/2/5). The `applyEthMessage` entry, `EthMessageRequest`, and the existing `EvmTraceScope` at `ApplyEthMessage.cpp:62-63`. The EthHost integration harness pattern from `test/opstack/EvmoneRefundSpikeTest.cpp` (`InMemoryStateView`, `state::State`, `state::EthHost`, `evmc::VM`).
- Produces: no new symbols — this task wires existing ones. After this task, running any Eth tx with an active policy captures a call tree; with policy off, `applyEthMessage` behaves exactly as before (no tracer attached).

- [ ] **Step 1: Write the failing integration test**

Create `bcos-evm/test/eth/TraceCallTreeTest.cpp`. It builds two contracts — outer (`0x01`) CALLs inner (`0x02`), inner is STOP — attaches an active session, executes, and asserts a 2-frame nested tree:

```cpp
#define BOOST_TEST_MODULE TraceCallTreeTest
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/trace/ExecutionTraceSession.h"
#include "bcos-evm/eth/trace/TraceCollector.h"
#include "bcos-evm/eth/trace/TraceGate.h"
#include "bcos-evm/eth/trace/TracePolicy.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::trace::test
{
namespace
{
struct GlobalPolicyGuard
{
    explicit GlobalPolicyGuard(TracePolicy policy) { TraceGate::setGlobal(std::move(policy)); }
    ~GlobalPolicyGuard() { TraceGate::setGlobal(TracePolicy{}); }
};

evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TraceCallTreeTest)

// outer(0x01): CALL 0x02 with zero value, then STOP. inner(0x02): STOP.
// Expect two frames: depth-1 inner (parent = outer frameId) + depth-0 outer.
BOOST_AUTO_TEST_CASE(captures_nested_call_tree)
{
    TracePolicy policy;
    policy.enabled = true;
    policy.level = TraceLevel::CallTree;
    GlobalPolicyGuard guard(policy);

    state::test::InMemoryStateView view;

    state::Account outer;
    outer.code = bcos::fromHex("60006000600060006000600260fff100");  // CALL 0x02; STOP
    view.insert_account(addr(0x01), outer);

    state::Account inner;
    inner.code = bcos::fromHex("00");  // STOP
    view.insert_account(addr(0x02), inner);

    state::State state(view);
    evmc_tx_context txContext{};
    txContext.tx_origin = addr(0xaa);
    txContext.block_gas_limit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm::RevisionConfig cfg{.revision = EVMC_SHANGHAI};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 1'000'000;
    msg.sender = addr(0xaa);
    msg.recipient = addr(0x01);
    msg.code_address = addr(0x01);

    ExecutionTraceSession session(vm, makeTraceContext("eth", 1));
    BOOST_REQUIRE(session.active());

    auto const result = vm.execute(host, EVMC_SHANGHAI, msg, outer.code.data(), outer.code.size());
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

    auto* collector = session.collector();
    BOOST_REQUIRE(collector != nullptr);
    BOOST_REQUIRE_EQUAL(collector->frames().size(), 2u);

    // Locate the two frames by depth (inner is emitted first on its execution end).
    CallFrameEvent const* top = nullptr;
    CallFrameEvent const* nested = nullptr;
    for (auto const& f : collector->frames())
    {
        if (f.depth == 0) { top = &f; }
        if (f.depth == 1) { nested = &f; }
    }
    BOOST_REQUIRE(top != nullptr);
    BOOST_REQUIRE(nested != nullptr);
    BOOST_CHECK_EQUAL(top->parentFrameId, 0u);
    BOOST_CHECK(top->fromEvmone);
    BOOST_CHECK_EQUAL(nested->parentFrameId, top->frameId);
    BOOST_CHECK(nested->fromEvmone);
}

// §16 criterion 1 (functional) on the production VM path: with policy OFF, the same nested
// call runs identically and the VM this session was constructed over (== applyEthMessage's
// input.vm) has NO tracer attached after execution, and no collector is armed.
BOOST_AUTO_TEST_CASE(disabled_policy_leaves_input_vm_untouched)
{
    TraceGate::setGlobal(TracePolicy{});  // explicit OFF (defaults are off)

    state::test::InMemoryStateView view;
    state::Account outer;
    outer.code = bcos::fromHex("60006000600060006000600260fff100");  // CALL 0x02; STOP
    view.insert_account(addr(0x01), outer);
    state::Account inner;
    inner.code = bcos::fromHex("00");  // STOP
    view.insert_account(addr(0x02), inner);

    state::State state(view);
    evmc_tx_context txContext{};
    txContext.tx_origin = addr(0xaa);
    txContext.block_gas_limit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm::RevisionConfig cfg{.revision = EVMC_SHANGHAI};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 1'000'000;
    msg.sender = addr(0xaa);
    msg.recipient = addr(0x01);
    msg.code_address = addr(0x01);

    {
        ExecutionTraceSession session(vm, makeTraceContext("eth", 1));
        BOOST_CHECK(!session.active());
        BOOST_CHECK(session.collector() == nullptr);
        BOOST_CHECK(TraceGate::collector() == nullptr);

        auto const result =
            vm.execute(host, EVMC_SHANGHAI, msg, outer.code.data(), outer.code.size());
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

        // No tracer attached during execution; no collector armed → bcos hooks are no-ops.
        BOOST_CHECK(static_cast<evmone::VM*>(vm.get_raw_pointer())->get_tracer() == nullptr);
        BOOST_CHECK(TraceGate::collector() == nullptr);
    }
    // And nothing left attached after the (inactive) session tears down.
    BOOST_CHECK(static_cast<evmone::VM*>(vm.get_raw_pointer())->get_tracer() == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::trace::test
```

> This second case additionally needs `#include <evmone/vm.hpp>` (for `evmone::VM::get_tracer()`) in the test's include block; `TraceGate.h` and `TracePolicy.h` are already included for the first case.

- [ ] **Step 2: Run test to verify it fails**

Add the target first (Step 5 cmake block), then:

Run: `cmake --build <build-dir> --target TraceCallTreeTest`
Expected: FAIL to link/compile until the session is usable in this context — but since Tasks 1-5 are done, this test should actually **compile and run**; it validates the session+tracer+EthHost integration. If frames are not captured (e.g. `frames().size() != 2`), that is the failing state driving the `applyEthMessage` wiring below. Run it and confirm it passes on the trace stack; then proceed to wire `applyEthMessage`.

> **Rationale:** the nested-tree behavior lives entirely in Tasks 4-5 (tracer + session). This test locks it in at the EthHost level. The `applyEthMessage` edit (Steps 3-4) makes production Eth txs create the session automatically; its "no change when off" guarantee is covered by the existing apply / EEST suites, which must still pass after the edit.

- [ ] **Step 3: Add the session to `applyEthMessage`**

In `bcos-evm/eth/apply/ApplyEthMessage.cpp`, add the include near the other trace include (after line 9 `#include "bcos-evm/eth/trace/EvmTrace.h"`):

```cpp
#include "bcos-evm/eth/trace/ExecutionTraceSession.h"
#include "bcos-evm/eth/trace/TraceGate.h"
```

Then insert the session immediately after the existing `EvmTraceScope` (currently lines 62-63):

```cpp
    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("eth", input.blockInfo.number, input.txHash));
    trace::ExecutionTraceSession traceSession(
        *input.vm, trace::makeTraceContext("eth", input.blockInfo.number, input.txHash));
```

- [ ] **Step 4: Emit summaries at the two exit points**

In the buyGas-reject early return (currently lines 99-108), set a reject summary before `co_return output;`:

```cpp
        if (!co_await coordinator.buyGas(feeView, output))
        {
            // GST/geth insufficient-funds: reject with unchanged state (no penalty diff).
            if (ctx.state.has_checkpoint())
            {
                ctx.state.revert();
            }
            output.gasUsed = 0;
            output.stateDiff = ctx.state.build_diff();
            if (traceSession.active())
            {
                trace::ExecutionSummary summary;
                summary.status = EVMC_REJECTED;
                summary.exitKind = StateTransitionExitKind::GasAffordRejected;
                summary.reachedEvmEntry = false;
                summary.gasLimit = input.message.gas;
                traceSession.setSummary(summary);
            }
            co_return output;
        }
```

At the normal end, before the final `co_return output;` (currently line 137):

```cpp
    if (traceSession.active())
    {
        trace::ExecutionSummary summary;
        summary.status = ctx.evmcResult.status_code;
        summary.exitKind = ctx.exitKind;
        summary.reachedEvmEntry = true;
        summary.gasLimit = input.message.gas;
        summary.gasUsed = output.gasUsed;
        traceSession.setSummary(summary);
    }

    co_return output;
```

- [ ] **Step 5: Register the integration test in `TraceTests.cmake`**

Append a second block after the unit `foreach` (integration tests link the fuller stack + test helpers, mirroring `PragueStateTest`):

```cmake
# Integration tests: exercise session + tracer over the real EthHost / State path.
set(TRACE_CALL_TREE_TEST TraceCallTreeTest)
add_executable(${TRACE_CALL_TREE_TEST} eth/${TRACE_CALL_TREE_TEST}.cpp)
target_include_directories(${TRACE_CALL_TREE_TEST} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
# Mirrors EvmoneRefundSpikeTest (StateTests.cmake:138-146); InMemoryStateView.h is header-only,
# so bcos-evm-test-state is NOT needed here.
target_link_libraries(${TRACE_CALL_TREE_TEST} PRIVATE
    bcos-evm-eth
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)
add_test(NAME ${TRACE_CALL_TREE_TEST} COMMAND ${TRACE_CALL_TREE_TEST})
```

- [ ] **Step 6: Build the trace test + the existing apply/EEST regression, verify pass**

Run: `cmake --build <build-dir> --target TraceCallTreeTest && ctest --test-dir <build-dir> -R "TraceCallTree" --output-on-failure`
Expected: PASS — 1 case, 2 frames captured, correct parent linkage.

Run (regression — `applyEthMessage` unchanged when off): `ctest --test-dir <build-dir> -R "PragueState|EvmoneRefundSpike" --output-on-failure`
Expected: PASS — unchanged behavior with trace off.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/apply/ApplyEthMessage.cpp bcos-evm/test/eth/TraceCallTreeTest.cpp bcos-evm/test/cmake/TraceTests.cmake
rtk git commit -m "feat(evm-trace): wire ExecutionTraceSession into applyEthMessage + nested tree test (Phase 0)"
```

---

## Task 7: bcos fast-path + transfer leaf-frame hooks

**Files:**
- Create: `bcos-evm/eth/trace/FastPathFrameHook.h`
- Modify: `bcos-evm/eth/kernel/execution/EvmCallFrame.cpp` (2 hook call sites + include)
- Create: `bcos-evm/test/eth/TraceFastPathTest.cpp` (integration: CALL to identity precompile 0x04)
- Modify: `bcos-evm/test/cmake/TraceTests.cmake`

**Interfaces:**
- Consumes: `TraceGate::collector()` (Task 2), `TraceCollector::nextFrameId()` + `onCallFrame()` (Task 3), `CallFrameEvent` (Task 1). Hook call sites: `execution::runCallTargetFastPath` early return (`EvmCallFrame.cpp:508-511`) and the non-CREATE `transferOrFail` early return (`EvmCallFrame.cpp:564-567`); both inside `runFrameSteps` (`EvmCallFrame.cpp:501`). Re-verify these line numbers at implementation time — they drift as the file changes; anchor on the symbol names.
- Produces:
  - `void bcos::evm::trace::recordFastPathFrame(evmc_message const& msg, evmc_result const& result, std::string_view exitStep) noexcept;` — inline header helper; no-op when `TraceGate::collector()` is null. Emits a leaf `CallFrameEvent` with `fromEvmone=false`.
- Scope note: Phase 0 sets `parentFrameId=0` for bcos leaf frames (the enclosing evmone frame id is not exposed to this layer yet); precise parent correlation and the CREATE-path `transferOrFail` hooks are **Phase 1**.

- [ ] **Step 1: Write the failing integration test**

Create `bcos-evm/test/eth/TraceFastPathTest.cpp` — outer(`0x01`) does `CALL 0x04` (identity precompile), which short-circuits through `runCallTargetFastPath` (evmone never sees it):

```cpp
#define BOOST_TEST_MODULE TraceFastPathTest
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/trace/ExecutionTraceSession.h"
#include "bcos-evm/eth/trace/TraceCollector.h"
#include "bcos-evm/eth/trace/TraceGate.h"
#include "bcos-evm/eth/trace/TracePolicy.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::trace::test
{
namespace
{
struct GlobalPolicyGuard
{
    explicit GlobalPolicyGuard(TracePolicy policy) { TraceGate::setGlobal(std::move(policy)); }
    ~GlobalPolicyGuard() { TraceGate::setGlobal(TracePolicy{}); }
};

evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TraceFastPathTest)

BOOST_AUTO_TEST_CASE(records_precompile_frame_from_bcos_hook)
{
    TracePolicy policy;
    policy.enabled = true;
    policy.level = TraceLevel::CallTree;
    GlobalPolicyGuard guard(policy);

    state::test::InMemoryStateView view;
    state::Account outer;
    // PUSH1 0 x5 (retLen,retOff,argsLen,argsOff,value); PUSH1 04 (addr); PUSH2 FFFF (gas); CALL; STOP
    outer.code = bcos::fromHex("60006000600060006000600461fffff100");
    view.insert_account(addr(0x01), outer);

    state::State state(view);
    evmc_tx_context txContext{};
    txContext.tx_origin = addr(0xaa);
    txContext.block_gas_limit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm::RevisionConfig cfg{.revision = EVMC_SHANGHAI};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 1'000'000;
    msg.sender = addr(0xaa);
    msg.recipient = addr(0x01);
    msg.code_address = addr(0x01);

    ExecutionTraceSession session(vm, makeTraceContext("eth", 1));
    BOOST_REQUIRE(session.active());

    auto const result = vm.execute(host, EVMC_SHANGHAI, msg, outer.code.data(), outer.code.size());
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);

    auto* collector = session.collector();
    BOOST_REQUIRE(collector != nullptr);

    bool foundFastPath = false;
    bool foundEvmone = false;
    for (auto const& f : collector->frames())
    {
        if (!f.fromEvmone && f.exitStep == std::string_view("fastPath") &&
            f.codeAddress.bytes[19] == 0x04)
        {
            foundFastPath = true;
        }
        if (f.fromEvmone && f.depth == 0)
        {
            foundEvmone = true;
        }
    }
    BOOST_CHECK(foundFastPath);  // precompile leaf captured by bcos hook
    BOOST_CHECK(foundEvmone);    // outer contract captured by evmone tracer
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::trace::test
```

> **Note for implementer:** if `classifyCallTarget` with a null `callTargetPort` does not route `0x04` to `BuiltinPrecompile` in this build, use the existing precompile-routing test setup (grep `classifyCallTarget` / `BuiltinPrecompile` under `bcos-evm/eth/`) to configure the harness; the assertion (`exitStep=="fastPath"`, `fromEvmone=false`) is the invariant to preserve.

- [ ] **Step 2: Run test to verify it fails**

Register the target (Step 5) then:

Run: `cmake --build <build-dir> --target TraceFastPathTest && ctest --test-dir <build-dir> -R TraceFastPath --output-on-failure`
Expected: FAIL — `foundFastPath` is false (no bcos hook emits the precompile leaf frame yet).

- [ ] **Step 3: Create `FastPathFrameHook.h`**

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Leaf-frame trace hook for bcos short-circuit paths evmone never sees
 *        (precompile / empty-account envelope, insufficient-balance transfer).
 * @file FastPathFrameHook.h
 */
#pragma once

#include "bcos-evm/eth/trace/TraceCollector.h"
#include "bcos-evm/eth/trace/TraceGate.h"
#include "bcos-evm/eth/trace/TraceTypes.h"
#include <evmc/evmc.h>
#include <string_view>

namespace bcos::evm::trace
{
/// Emit a leaf CallFrameEvent (fromEvmone=false) for a frame that completed without evmone.
/// No-op when no session is active (one thread-local pointer load + branch — P1 zero-overhead).
inline void recordFastPathFrame(
    evmc_message const& msg, evmc_result const& result, std::string_view exitStep) noexcept
{
    auto* collector = TraceGate::collector();
    if (collector == nullptr)
    {
        return;
    }
    CallFrameEvent event;
    event.frameId = collector->nextFrameId();
    event.parentFrameId = 0;  // Phase 1: correlate with enclosing evmone frame
    event.depth = msg.depth;
    event.kind = msg.kind;
    event.from = msg.sender;
    event.to = msg.recipient;
    event.codeAddress = msg.code_address;
    event.gasIn = msg.gas;
    event.gasOut = result.gas_left;
    event.gasUsed = msg.gas - result.gas_left;
    event.status = result.status_code;
    event.exitStep = exitStep;
    event.fromEvmone = false;
    event.value = msg.value;
    collector->onCallFrame(event);
}
}  // namespace bcos::evm::trace
```

- [ ] **Step 4: Add the two hook call sites in `EvmCallFrame.cpp`**

Add the include with the other `bcos-evm/eth/...` includes near the top of `bcos-evm/eth/kernel/execution/EvmCallFrame.cpp`:

```cpp
#include "bcos-evm/eth/trace/FastPathFrameHook.h"
```

In `runFrameSteps`, wrap the fast-path early return (currently lines 508-511):

```cpp
    // Step 2: precompile / empty-account / policy fast path.
    if (auto early = runCallTargetFastPath(work, scope))
    {
        trace::recordFastPathFrame(work.callMessage(), early->result.raw(), "fastPath");
        return std::move(*early);
    }
```

And the non-CREATE value-transfer early return (currently lines 564-567):

```cpp
    else if (auto early = transferOrFail(work, scope))
    {
        trace::recordFastPathFrame(work.callMessage(), early->result.raw(), "transferOrFail");
        return std::move(*early);
    }
```

- [ ] **Step 5: Register the integration test in `TraceTests.cmake`**

Append after the `TraceCallTreeTest` block:

```cmake
set(TRACE_FAST_PATH_TEST TraceFastPathTest)
add_executable(${TRACE_FAST_PATH_TEST} eth/${TRACE_FAST_PATH_TEST}.cpp)
target_include_directories(${TRACE_FAST_PATH_TEST} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(${TRACE_FAST_PATH_TEST} PRIVATE
    bcos-evm-eth
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)
add_test(NAME ${TRACE_FAST_PATH_TEST} COMMAND ${TRACE_FAST_PATH_TEST})
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build <build-dir> --target TraceFastPathTest && ctest --test-dir <build-dir> -R TraceFastPath --output-on-failure`
Expected: PASS — `foundFastPath` and `foundEvmone` both true.

Run (regression — hooks are inert when off): `ctest --test-dir <build-dir> -R "PragueState|EvmoneRefundSpike|TraceCallTree" --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/trace/FastPathFrameHook.h bcos-evm/eth/kernel/execution/EvmCallFrame.cpp bcos-evm/test/eth/TraceFastPathTest.cpp bcos-evm/test/cmake/TraceTests.cmake
rtk git commit -m "feat(evm-trace): capture precompile/transfer leaf frames via bcos hooks (Phase 0)"
```

---

## Phase 0 Definition of Done

- All 8 test executables pass: `TracePolicyTest`, `TracePolicyOverrideTest`, `TraceCollectorTest`, `EvmCallTreeTracerTest`, `ExecutionTraceSessionTest`, `TraceCallTreeTest`, `TraceFastPathTest`, plus the existing `PragueState` / `EvmoneRefundSpike` regressions.
- **P1 (switchable, no overhead when off):** with policy off, `ExecutionTraceSession` is inactive, `TraceGate::collector()` is `nullptr`, no evmone tracer is attached to `input.vm`, and the bcos hooks are single-branch no-ops. Verified on the bare VM by `ExecutionTraceSessionTest::disabled_policy_attaches_nothing` **and** on the production EthHost/`input.vm` path by `TraceCallTreeTest::disabled_policy_leaves_input_vm_untouched` (the §16 criterion-1 functional check).
- **No cross-tx leakage (session path):** the session `remove_tracers()` at teardown; `ExecutionTraceSessionTest::enabled_policy_attaches_and_detaches` asserts the VM has no tracer after the scope. **Scope:** this covers the session-managed `input.vm` only; the legacy `EvmOpcodeProbe` attach-once leak (trace-off + `EEST_OPCODE_TRACE=1`) is **not** fixed in Phase 0 — see the "Scope of the leak fix" note above; both probe attach sites are removed in Phase 1.
- **P2 (reuse-ready):** all code in `eth/trace/`, linked only into `bcos-evm-eth`; no test dependency; `ExecutionTraceSession` drives the same `applyEthMessage` path a future `TraceService::replay()` will reuse.
- **Hybrid coverage:** evmone frames (`fromEvmone=true`) + bcos fast-path frames (`fromEvmone=false`) both land in one `TraceCollector` (verified by `TraceCallTreeTest` + `TraceFastPathTest`).

**Deferred to Phase 1 (not in this plan):** `GasLedger`, `HostEvent`s (balance/nonce/storage/selfdestruct/warm-cold/refund/transient/journal), `AccountStateDelta`, `LogTraceSink`, `RingBufferSink`, `EestFailureReporter`, `capture_on_error` armed path, `STATICCALL` enum extension, precise bcos↔evmone parent correlation. **Deferred to Phase 3:** `EVM_EXECUTION_TRACE` compile-strip (Tier B) + benchmark harness.
