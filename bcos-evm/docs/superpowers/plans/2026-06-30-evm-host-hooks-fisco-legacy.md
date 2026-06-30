# EvmHostHooks FISCO Legacy (P0) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `fixStorageStatus` / `fixNonceInit` bool fields from `eth/` kernel and implement FISCO legacy SSTORE / top-level CREATE nonce semantics via `EvmHostHooks` overrides in `FiscoVmHostPolicy`.

**Architecture:** Extend `EvmHostHooks` with four virtual methods (default = geth/evmone standard). `EthHost::set_storage` and `ExecutionFrame::finalizeFrame` dispatch through `extension*` when present, else call shared default free functions. FISCO reads `FiscoRevisionConfig.fix_*` into `FiscoVmHostPolicy::RevisionFlags` and overrides only when legacy behavior is required.

**Tech Stack:** C++20, Boost.Test, evmone, CMake (`bcos-evm-eth` / `bcos-evm-bcos` static libs; `eth/*.cpp` auto-globbed).

**Spec:** `docs/superpowers/specs/2026-06-30-evm-host-hooks-fisco-legacy-design.md`

## Global Constraints

- **Zero behavior change:** existing characterization tests must pass unchanged (modulo test file moves/assertion updates).
- **ADR-005 Rule 1:** `eth/` must not `#include` `bcos/` or `opstack/`.
- **Single PR** migration; revert = revert one PR.
- **Non-goals (do not implement):** merge nested `applyCreateNonceSemantics` with top-level nonce hook; OpStack changes; P1 `OpStackEntry` rename.
- **CMake:** new `eth/state/EvmHostHooks.cpp` is picked up by `file(GLOB_RECURSE BCOS_EVM_ETH_SOURCES eth/*.cpp)` — no CMake edit required.
- **Open question resolved for this plan:** FISCO OFF SSTORE matrix moves to `test/bcos/FiscoSstoreStatusTest.cpp`; `test/state/SstoreStatusTest.cpp` keeps Ethereum-only (fix ON / default hook) cases.

---

## File Map

| File | Responsibility |
| --- | --- |
| `eth/state/EvmHostHooks.h` | Hook interface + forward declare `State` |
| `eth/state/EvmHostHooks.cpp` | **New.** EIP-3529 refund + precise classify defaults; virtual method bodies |
| `eth/state/EthHost.hpp/cpp` | SSTORE orchestration via hooks; drop bool |
| `eth/execution/EvmCallFrame.h/cpp` | Top-level CREATE → `finalizeTopLevelCreateNonce` |
| `eth/execution/TxExecutionRunner.cpp` | Simpler `EthHost` / `FrameExecutionEnv` wiring |
| `eth/InnerExecute.h` | Remove fix* fields |
| `eth/pipeline/EvmTxContextView.h` | Remove fix* fields |
| `bcos/FiscoVmHostPolicy.h/cpp` | FISCO overrides + `RevisionFlags.fix_storage_status` |
| `bcos/FiscoExecutionBundle.h` | Stop projecting fix* to view; inject flags into policy deps |
| `bcos/FiscoPrecheckPolicy.cpp` | Remove executeInput fix* copies |
| `test/bcos/FiscoSstoreStatusTest.cpp` | **New.** Legacy OFF matrix |
| `test/state/SstoreStatusTest.cpp` | Ethereum-only cases |
| `test/state/SstoreRefundTest.cpp` | Remove `input.fixStorageStatus` |
| `test/eth/EvmTxContextViewTest.cpp` | Remove fix* assertions |
| `test/cmake/BcosTests.cmake` | Register `FiscoSstoreStatusTest` |
| `docs/adr/027-execution-session-injection.md` | Tier-2 table update |
| `docs/eth-layer-design-review.md` | Remove fixStorageStatus mention |

---

### Task 1: `EvmHostHooks` interface + default SSTORE implementation

**Files:**
- Modify: `bcos-evm/eth/state/EvmHostHooks.h`
- Create: `bcos-evm/eth/state/EvmHostHooks.cpp`
- Test: `bcos-evm/test/state/SstoreStatusTest.cpp` (still passes after Task 2)

**Interfaces:**
- **Produces:**
  - `void bcos::evm::state::applySstoreRefundEip3529(State&, evmc_bytes32 const& current, evmc_bytes32 const& original, evmc_bytes32 const& newValue) noexcept`
  - `evmc_storage_status bcos::evm::state::classifyStorageStatusPrecise(evmc_bytes32 const& original, evmc_bytes32 const& current, evmc_bytes32 const& newValue) noexcept`
  - Four new `EvmHostHooks` virtual methods with default bodies calling the above

- [ ] **Step 1: Extend `EvmHostHooks.h`**

Add after existing includes / before struct:

```cpp
namespace bcos::evm::state
{
class State;

/// Shared helpers used by default hooks and FiscoVmHostPolicy (fix ON path).
void applySstoreRefundEip3529(State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& newValue) noexcept;

evmc_storage_status classifyStorageStatusPrecise(evmc_bytes32 const& original,
    evmc_bytes32 const& current, evmc_bytes32 const& newValue) noexcept;
```

Inside `struct EvmHostHooks`, after `bumpContractCreateNonce`:

```cpp
    virtual void applySstoreRefund(State& state, evmc_bytes32 const& current,
        evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept;

    virtual evmc_storage_status classifyStorageStatus(evmc_bytes32 const& original,
        evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept;

    virtual void applyLegacySstoreDeletedRefund(
        State& state, evmc_storage_status status) const noexcept;

    virtual void finalizeTopLevelCreateNonce(
        State& state, evmc_address const& createAddr) noexcept;
```

- [ ] **Step 2: Create `EvmHostHooks.cpp`**

Move logic from `EthHost.cpp` anonymous namespace + `EthHost::classifyStorageStatus(..., true)`:

```cpp
#include "bcos-evm/eth/state/EvmHostHooks.h"
#include "bcos-evm/eth/gas/Eip2929StorageGas.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::state
{
namespace
{
using bcos::evm::gas::COLD_SLOAD_COST_EIP2929;
using bcos::evm::gas::SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529;
using bcos::evm::gas::SSTORE_RESET_GAS_EIP2200;
using bcos::evm::gas::SSTORE_SET_GAS_EIP2200;
using bcos::evm::gas::WARM_STORAGE_READ_COST_EIP2929;
}  // namespace

void applySstoreRefundEip3529(State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& value) noexcept
{
    // Copy verbatim from EthHost.cpp lines 42-84 (today's implementation)
}

evmc_storage_status classifyStorageStatusPrecise(evmc_bytes32 const& oldValue,
    evmc_bytes32 const& currentValue, evmc_bytes32 const& newValue) noexcept
{
    // Copy verbatim from EthHost.cpp classifyStorageStatus when fixStorageStatus==true (lines 356-404)
}

void EvmHostHooks::applySstoreRefund(State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept
{
    applySstoreRefundEip3529(state, current, original, newValue);
}

evmc_storage_status EvmHostHooks::classifyStorageStatus(evmc_bytes32 const& original,
    evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept
{
    return classifyStorageStatusPrecise(original, current, newValue);
}

void EvmHostHooks::applyLegacySstoreDeletedRefund(
    State& /*state*/, evmc_storage_status /*status*/) const noexcept
{}

void EvmHostHooks::finalizeTopLevelCreateNonce(
    State& /*state*/, evmc_address const& /*createAddr*/) noexcept
{}
}  // namespace bcos::evm::state
```

- [ ] **Step 3: Build eth library**

Run from repo root (adjust build dir if needed):

```bash
cmake --build build --target bcos-evm-eth -j8
```

Expected: **PASS** (no consumers changed yet; duplicate symbols in EthHost.cpp until Task 2 — if link fails on duplicate, do Task 1+2 in one commit).

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/state/EvmHostHooks.h bcos-evm/eth/state/EvmHostHooks.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(evm): add EvmHostHooks SSTORE and CREATE nonce hook defaults

Move EIP-3529 refund and precise SSTORE status classification into
EvmHostHooks.cpp as the standard Ethereum default implementation.
EOF
)"
```

---

### Task 2: Wire `EthHost::set_storage` through hooks (remove bool param)

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.hpp`
- Modify: `bcos-evm/eth/state/EthHost.cpp`
- Test: `bcos-evm/test/state/SstoreStatusTest.cpp`

**Interfaces:**
- **Consumes:** `applySstoreRefundEip3529`, `classifyStorageStatusPrecise`, `EvmHostHooks` virtuals from Task 1
- **Produces:** `EthHost(..., EvmHostHooks* extension, ChainCallTargetDispatcher* chainPort)` — no `fixStorageStatus`

- [ ] **Step 1: Update `EthHost.hpp`**

Remove constructor `bool fixStorageStatus`, member `m_fixStorageStatus`, and private static `classifyStorageStatus(..., bool)`.

Constructor becomes:

```cpp
    EthHost(State& state, evmc_tx_context txContext,
        bcos::evm_standard::RevisionConfig revisionConfig, evmc::VM& vm, BlockHashes blockHashes,
        EvmHostHooks* extension = nullptr, ChainCallTargetDispatcher* chainPort = nullptr);
```

- [ ] **Step 2: Update `EthHost.cpp` constructor**

Remove `fixStorageStatus` parameter and `m_fixStorageStatus` initializer.

- [ ] **Step 3: Replace `set_storage` body**

```cpp
evmc_storage_status EthHost::set_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    auto const slot = std::make_pair(addr, key);
    auto [it, inserted] = m_storageOriginalValues.try_emplace(slot, evmc_bytes32{});
    if (inserted)
    {
        it->second = m_state.get_storage(addr, key);
    }

    auto const& original = it->second;
    auto const current = m_state.get_storage(addr, key);
    if (Bytes32Equal{}(current, value))
    {
        return EVMC_STORAGE_ASSIGNED;
    }

    if (m_extension != nullptr)
    {
        m_extension->applySstoreRefund(m_state, current, original, value);
    }
    else
    {
        applySstoreRefundEip3529(m_state, current, original, value);
    }

    m_state.set_storage(addr, key, value);

    auto const status = m_extension != nullptr ?
                            m_extension->classifyStorageStatus(original, current, value) :
                            classifyStorageStatusPrecise(original, current, value);

    if (m_extension != nullptr)
    {
        m_extension->applyLegacySstoreDeletedRefund(m_state, status);
    }

    return status;
}
```

Add `#include "bcos-evm/eth/state/EvmHostHooks.h"`.

- [ ] **Step 4: Delete moved/dead code from `EthHost.cpp`**

Remove:
- Anonymous namespace `applySstoreRefundEip3529` (now in EvmHostHooks.cpp)
- `EthHost::classifyStorageStatus(..., bool fixStorageStatus)` entire function
- FISCO comment on line ~118

- [ ] **Step 5: Fix direct `EthHost` test call sites**

In `test/state/SstoreStatusTest.cpp`, change:

```cpp
EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), nullptr,
    testCase.fixStorageStatus);
```

to:

```cpp
EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), nullptr);
```

For OFF-matrix cases (Task 6 will move them); temporarily expect compile break on OFF cases until Task 6 — **or** skip OFF loop in this task and only run ON cases.

Run:

```bash
cmake --build build --target SstoreStatusTest -j8
./build/bcos-evm/test/SstoreStatusTest
```

Expected: ON-matrix cases **PASS**; OFF cases removed or `@TODO` until Task 6.

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/state/EthHost.hpp bcos-evm/eth/state/EthHost.cpp bcos-evm/test/state/SstoreStatusTest.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(evm): dispatch EthHost SSTORE through EvmHostHooks

Remove fixStorageStatus from EthHost; extension=null uses standard
EIP-3529/precise status helpers from EvmHostHooks.cpp.
EOF
)"
```

---

### Task 3: `FiscoVmHostPolicy` SSTORE overrides

**Files:**
- Modify: `bcos-evm/bcos/FiscoVmHostPolicy.h`
- Modify: `bcos-evm/bcos/FiscoVmHostPolicy.cpp`
- Modify: `bcos-evm/bcos/FiscoExecutionBundle.h`
- Create: `bcos-evm/test/bcos/FiscoSstoreStatusTest.cpp`
- Modify: `bcos-evm/test/cmake/BcosTests.cmake`
- Modify: `bcos-evm/test/state/SstoreStatusTest.cpp` (remove OFF matrix)

**Interfaces:**
- **Consumes:** `applySstoreRefundEip3529`, `classifyStorageStatusPrecise`, `EvmHostHooks` base methods
- **Produces:** `FiscoVmHostPolicy` overrides; `RevisionFlags.fix_storage_status`

- [ ] **Step 1: Add `fix_storage_status` to `RevisionFlags` in `FiscoVmHostPolicy.h`**

```cpp
    struct RevisionFlags
    {
        bool fix_auth_check{false};
        bool use_raw_address{false};
        bool fix_storage_status{false};
        bool fix_nonce_init{false};
        bool web3Tx{false};
        int64_t createLevel{0};
    };
```

Declare overrides:

```cpp
    void applySstoreRefund(state::State& state, evmc_bytes32 const& current,
        evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept override;

    evmc_storage_status classifyStorageStatus(evmc_bytes32 const& original,
        evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept override;

    void applyLegacySstoreDeletedRefund(
        state::State& state, evmc_storage_status status) const noexcept override;

    void finalizeTopLevelCreateNonce(
        state::State& state, evmc_address const& createAddr) noexcept override;
```

- [ ] **Step 2: Implement overrides in `FiscoVmHostPolicy.cpp`**

```cpp
#include "bcos-evm/eth/gas/Eip2929StorageGas.h"
#include "bcos-evm/eth/state/EvmHostHooks.h"
#include "bcos-evm/eth/state/HashUtils.hpp"

void FiscoVmHostPolicy::applySstoreRefund(state::State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept
{
    if (m_revisionFlags.fix_storage_status)
    {
        state::applySstoreRefundEip3529(state, current, original, newValue);
    }
}

evmc_storage_status FiscoVmHostPolicy::classifyStorageStatus(evmc_bytes32 const& original,
    evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept
{
    if (m_revisionFlags.fix_storage_status)
    {
        return state::classifyStorageStatusPrecise(original, current, newValue);
    }
    return state::isZeroBytes32(newValue) ? EVMC_STORAGE_DELETED : EVMC_STORAGE_MODIFIED;
}

void FiscoVmHostPolicy::applyLegacySstoreDeletedRefund(
    state::State& state, evmc_storage_status status) const noexcept
{
    if (!m_revisionFlags.fix_storage_status && status == EVMC_STORAGE_DELETED)
    {
        state.add_refund(bcos::evm::gas::SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
    }
}

void FiscoVmHostPolicy::finalizeTopLevelCreateNonce(
    state::State& state, evmc_address const& createAddr) noexcept
{
    if (m_revisionFlags.fix_nonce_init && !state::isZeroAddress(createAddr))
    {
        state.set_nonce(createAddr, 1);
    }
}
```

- [ ] **Step 3: Wire `fix_storage_status` in `FiscoExecutionBundle.h`**

In `makeDeps`:

```cpp
        deps.revisionFlags.fix_storage_status = input.revisionConfig.fix_storage_status;
```

(Do **not** remove `m_view.fixStorageStatus` yet — Task 5.)

- [ ] **Step 4: Create `test/bcos/FiscoSstoreStatusTest.cpp`**

Copy OFF-matrix loop from `SstoreStatusTest.cpp` (`fixStorageStatus=false` rows) and use:

```cpp
FiscoVmHostPolicy::FiscoVmHostPolicyDeps deps;
deps.state = &state;
deps.revisionFlags.fix_storage_status = testCase.fixStorageStatus; // false for OFF rows
FiscoVmHostPolicy policy(false, std::move(deps));
EthHost host(state, evmc_tx_context{}, cfg, vm, emptyBlockHashes(), &policy);
```

- [ ] **Step 5: Register test in `test/cmake/BcosTests.cmake`**

Mirror `FiscoVmHostPolicyTest` block:

```cmake
set(FISCO_SSTORE_STATUS_TEST_BINARY_NAME FiscoSstoreStatusTest)
add_executable(${FISCO_SSTORE_STATUS_TEST_BINARY_NAME}
    bcos/FiscoSstoreStatusTest.cpp
)
target_link_libraries(${FISCO_SSTORE_STATUS_TEST_BINARY_NAME} PRIVATE bcos-evm-bcos)
add_test(NAME FiscoSstoreStatus COMMAND ${FISCO_SSTORE_STATUS_TEST_BINARY_NAME})
```

- [ ] **Step 6: Trim `SstoreStatusTest.cpp` to Ethereum-only (fix ON / null extension)**

Keep cases with `fixStorageStatus=true` only; delete OFF rows and `fixStorageStatus` struct field.

- [ ] **Step 7: Run tests**

```bash
cmake --build build --target SstoreStatusTest FiscoSstoreStatusTest -j8
./build/bcos-evm/test/SstoreStatusTest
./build/bcos-evm/test/FiscoSstoreStatusTest
```

Expected: **All PASS**

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/bcos/FiscoVmHostPolicy.h bcos-evm/bcos/FiscoVmHostPolicy.cpp \
  bcos-evm/bcos/FiscoExecutionBundle.h bcos-evm/test/bcos/FiscoSstoreStatusTest.cpp \
  bcos-evm/test/cmake/BcosTests.cmake bcos-evm/test/state/SstoreStatusTest.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos): FiscoVmHostPolicy SSTORE legacy hooks

Implement fix_storage_status behavior via EvmHostHooks overrides;
move OFF-matrix SSTORE tests to FiscoSstoreStatusTest.
EOF
)"
```

---

### Task 4: `ExecutionFrame` top-level CREATE nonce hook

**Files:**
- Modify: `bcos-evm/eth/execution/EvmCallFrame.h`
- Modify: `bcos-evm/eth/execution/ExecutionFrame.cpp`
- Modify: `bcos-evm/eth/execution/TxExecutionRunner.cpp` (partial — only `FrameExecutionEnv` ctor args)

**Interfaces:**
- **Consumes:** `EvmHostHooks::finalizeTopLevelCreateNonce`
- **Produces:** `FrameExecutionEnv` without `fixNonceInit`

- [ ] **Step 1: Update `ExecutionFrame.h`**

Remove `bool fixNonceInit{false};` and constructor parameter `fixNonceInit_`:

```cpp
    FrameExecutionEnv(state::State& state_, evmc::VM& vm_,
        bcos::evm_standard::RevisionConfig const& revisionConfig_, state::EvmHostHooks* extension_,
        evmc_address txOrigin_, evmc_address& executionAddress_,
        ChainCallTargetDispatcher* chainPort_ = nullptr) noexcept
```

- [ ] **Step 2: Update `ExecutionFrame.cpp` `finalizeFrame`**

Replace block at lines ~237-247:

```cpp
        if (scope == FrameScope::TopLevel)
        {
            if (isCreateKind(callMessage.kind) && work.ctx.extension != nullptr)
            {
                auto const createAddr = resolveCreateAddress(callMessage, result.raw());
                work.ctx.extension->finalizeTopLevelCreateNonce(work.ctx.state, createAddr);
            }
        }
```

Remove `work.ctx.fixNonceInit` condition.

- [ ] **Step 3: Update `TxExecutionRunner.cpp` `FrameExecutionEnv` construction**

Change:

```cpp
    execution::FrameExecutionEnv frameCtx{state, *input.vm, input.revisionConfig, input.extension,
        txContext.tx_origin, host.execution_address_ref(), input.fixNonceInit, input.chainPort};
```

to:

```cpp
    execution::FrameExecutionEnv frameCtx{state, *input.vm, input.revisionConfig, input.extension,
        txContext.tx_origin, host.execution_address_ref(), input.chainPort};
```

Also update `EthHost` construction (remove `input.fixStorageStatus` if still present):

```cpp
    state::EthHost host(state, txContext, input.revisionConfig, *input.vm, input.blockHashes,
        input.extension, input.chainPort);
```

- [ ] **Step 4: Grep for remaining `fixNonceInit` / `FrameExecutionEnv` old ctor**

```bash
rg 'fixNonceInit|fixStorageStatus' bcos-evm/eth bcos-evm/test
```

Fix any test direct `FrameExecutionEnv` constructions.

- [ ] **Step 5: Build + smoke**

```bash
cmake --build build --target bcos-evm-eth bcos-evm-bcos -j8
```

Expected: **PASS**

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/execution/EvmCallFrame.h bcos-evm/eth/execution/ExecutionFrame.cpp \
  bcos-evm/eth/execution/TxExecutionRunner.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(evm): top-level CREATE nonce via EvmHostHooks

Replace FrameExecutionEnv.fixNonceInit with extension->finalizeTopLevelCreateNonce.
EOF
)"
```

---

### Task 5: Remove eth pipeline bool fields + bcos propagation

**Files:**
- Modify: `bcos-evm/eth/InnerExecute.h`
- Modify: `bcos-evm/eth/pipeline/EvmTxContextView.h`
- Modify: `bcos-evm/bcos/FiscoExecutionBundle.h`
- Modify: `bcos-evm/bcos/FiscoPrecheckPolicy.cpp`
- Modify: `bcos-evm/test/state/SstoreRefundTest.cpp`
- Modify: `bcos-evm/test/eth/EvmTxContextViewTest.cpp`

- [ ] **Step 1: Delete from `ExecuteMessage.h`**

Remove lines:

```cpp
    bool fixStorageStatus{true};
    bool fixNonceInit{false};
```

- [ ] **Step 2: Delete from `EvmTxContextView.h`**

Remove fields and from `toExecuteMessageInput`:

```cpp
        input.fixStorageStatus = fixStorageStatus;
        input.fixNonceInit = fixNonceInit;
```

- [ ] **Step 3: Clean `FiscoExecutionBundle.h`**

Remove:

```cpp
        m_view.fixStorageStatus = input.revisionConfig.fix_storage_status;
        m_view.fixNonceInit = input.revisionConfig.fix_nonce_init;
```

Keep `makeDeps` revisionFlags wiring from Task 3.

- [ ] **Step 4: Clean `FiscoPrecheckPolicy.cpp`**

Remove:

```cpp
    executeInput.fixStorageStatus = m_input.revisionConfig.fix_storage_status;
    executeInput.fixNonceInit = m_input.revisionConfig.fix_nonce_init;
```

- [ ] **Step 5: Fix `SstoreRefundTest.cpp`**

Delete line 85: `input.fixStorageStatus = true;` (default hook path is standard).

- [ ] **Step 6: Fix `EvmTxContextViewTest.cpp`**

Remove lines 58-59:

```cpp
    BOOST_CHECK_EQUAL(fromSession.fixStorageStatus, fromLegacy.fixStorageStatus);
    BOOST_CHECK_EQUAL(fromSession.fixNonceInit, fromLegacy.fixNonceInit);
```

- [ ] **Step 7: Zero-identifier gate**

```bash
rg 'fixStorageStatus|fixNonceInit' bcos-evm/eth
```

Expected: **0 matches**

```bash
rg 'fixStorageStatus|fixNonceInit' bcos-evm/bcos-evm bcos-evm/bcos bcos-evm/test
```

Expected: only `FiscoRevisionConfig` / test variable names like `fix_storage_status` in policy tests — **not** eth pipeline fields.

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/eth/InnerExecute.h bcos-evm/eth/pipeline/EvmTxContextView.h \
  bcos-evm/bcos/FiscoExecutionBundle.h bcos-evm/bcos/FiscoPrecheckPolicy.cpp \
  bcos-evm/test/state/SstoreRefundTest.cpp bcos-evm/test/eth/EvmTxContextViewTest.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(evm): remove fixStorageStatus/fixNonceInit from eth pipeline

FISCO legacy flags now flow only into FiscoVmHostPolicy RevisionFlags.
EOF
)"
```

---

### Task 6: Documentation sync

**Files:**
- Modify: `bcos-evm/docs/adr/027-execution-session-injection.md`
- Modify: `bcos-evm/docs/eth-layer-design-review.md`

- [ ] **Step 1: ADR-027 Tier-2 table**

Replace Tier-2 row:

```markdown
| **2 — execution infra** | `vm*`, `blockHashes` | Set in Bundle factory from request/revision; copied via `wire()` |
```

Remove `fixStorageStatus`, `fixNonceInit` from Tier-2 and from `ExecutionSession` code sample (~lines 73-74).

Add footnote: FISCO `fix_storage_status` / `fix_nonce_init` live in `FiscoVmHostPolicy::RevisionFlags` (Tier-1 extension behavior).

- [ ] **Step 2: `eth-layer-design-review.md`**

Delete bullet about `fixStorageStatus` flag in EthHost section (~line 61).

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/docs/adr/027-execution-session-injection.md bcos-evm/docs/eth-layer-design-review.md
rtk git commit -m "$(cat <<'EOF'
docs: ADR-027 Tier-2 drops fixStorageStatus/fixNonceInit

Document FISCO legacy SSTORE/nonce flags as VmHostPolicy hooks.
EOF
)"
```

---

### Task 7: Final verification gate

- [ ] **Step 1: Build targets**

```bash
cmake --build build --target bcos-evm-eth bcos-evm-bcos \
  SstoreStatusTest SstoreRefundTest FiscoSstoreStatusTest FiscoVmHostPolicyTest \
  FiscoExecuteSmokeTest EvmTxContextViewTest RevisionConfigProfileTest -j8
```

Expected: **All targets build**

- [ ] **Step 2: CTest filter**

```bash
cd build && ctest -R 'SstoreStatus|SstoreRefund|FiscoSstore|FiscoVmHost|FiscoExecuteSmoke|EvmTxContextView|RevisionConfigProfile' --output-on-failure
```

Expected: **100% passed**

- [ ] **Step 3: Gap 38 include audit**

```bash
rg '#include.*bcos-evm/(bcos|opstack)' bcos-evm/eth
```

Expected: **0 matches**

- [ ] **Step 4: Success criteria checklist (from spec §11)**

- [ ] `eth/` 无 `fixStorageStatus` / `fixNonceInit`
- [ ] `EthHost` 无 FISCO 构造参数
- [ ] `FiscoExecutionBundle` 不设置 `view.fix*`
- [ ] ctest 全绿
- [ ] Gap 38 通过

---

## Spec Coverage Self-Review

| Spec section | Task |
| --- | --- |
| §4.1 EvmHostHooks API | Task 1 |
| §4.2 Default impl location | Task 1 |
| §4.3 EthHost set_storage flow | Task 2 |
| §4.4 FiscoVmHostPolicy overrides | Task 3 |
| §4.5 ExecutionFrame | Task 4 |
| §5 File list eth/bcos/test | Tasks 1-5 |
| §7 Testing | Tasks 3, 5, 7 |
| §8 Migration order | Tasks 1→7 |
| §11 Success criteria | Task 7 |
| §10 Open Q1 test location | Task 3 (`FiscoSstoreStatusTest`) |
| ADR-027 Tier change | Task 6 |

**No placeholders.** All steps include concrete paths and code.

---

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-06-30-evm-host-hooks-fisco-legacy.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — implement all tasks in this session with checkpoints  

Which approach do you want?
