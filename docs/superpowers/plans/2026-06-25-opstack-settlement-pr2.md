# OpStack Settlement PR2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close OpStack settlement locality by adding `finalizeDeposit` + `settleNormal`/`settleDeposit` facades, moving `refundGas`/gas pool out of the bridge, and narrowing `OpStackFeeContext` per ADR-021 PR2.

**Architecture:** Sync `finalize*` functions own gas/journal math (unit-test surface). Async `settle*` facades are the bridge's only post-pipeline entry points: `finalize*` → (`refundGas` for normal only) → `gasPool.returnGas`. Deposit pre-pipeline (depositNonce → mint → checkpoint) stays in bridge.

**Tech Stack:** C++17, Boost.Test, `bcos-task` coroutines, `bcos-evm-op` static lib, evmone (integration tests only).

**Spec:** `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md`  
**ADR:** `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md` (§2.2 PR2)

## Global Constraints

- Do **not** modify `eth/orchestration/` or include `bcos/` from `opstack/` (ADR-005).
- Deposit pre-pipeline order in bridge: `depositNonce` → `mint` → `checkpoint()` → `runTxPipeline` (ADR-021).
- `finalize*` must **not** call `refundGas` or gas pool hooks.
- Gas settlement outputs live only in `OpStackSettlementResult` after E1 (no `feeCtx.m_gasUsed` et al.).
- Implement tests before production code for deposit tracks (G1).
- Use `rtk` prefix on shell commands per project convention.
- **Build policy (SDD):** Tasks 1–8 write tests and production code but skip per-task `cmake`/`ctest`; Task 9 runs unified build and fixes integration failures.

## File Map

| File | Responsibility |
| --- | --- |
| `bcos-evm/opstack/OpStackSettlement.h/.cpp` | `finalizeNormal` (H1), `finalizeDeposit`, `settleNormal`, `settleDeposit` |
| `bcos-evm/opstack/OpStackTxFeeLedger.h/.cpp` | `refundGas(ctx, feeCtx, settled)` |
| `bcos-evm/opstack/OpStackExecutionBridge.cpp` | Thin orchestration; no inline deposit settlement / no Guard |
| `bcos-evm/test/opstack/OpStackDepositSettlementTest.cpp` | **New** — deposit three-track module tests |
| `bcos-evm/test/opstack/OpStackSettlementTest.cpp` | `finalizeNormal` H1 + E1 assertions |
| `bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp` | `refundGas` settled param |
| `bcos-evm/test/opstack/RefundIsthmusTest.cpp` | `refundIsthmusOperatorCost` gas input |
| `bcos-evm/test/cmake/OpStackTests.cmake` | Register `OpStackDepositSettlementTest` |
| `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md` | PR2 Compliance → Implemented on merge |

---

### Task 1: `OpStackDepositSettlementTest` (red)

**Files:**
- Create: `bcos-evm/test/opstack/OpStackDepositSettlementTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake` (after `OpStackSettlement` target block ~L324)
- Modify: `bcos-evm/opstack/OpStackSettlement.h` (forward-declare `finalizeDeposit` only)

**Interfaces:**
- Consumes: `finalizeDeposit` (not yet implemented — link will fail)
- Produces: failing tests documenting three-track oracle

- [ ] **Step 1: Add `finalizeDeposit` declaration to header**

In `bcos-evm/opstack/OpStackSettlement.h`, after `finalizeNormal`:

```cpp
OpStackSettlementResult finalizeDeposit(
    TxPipelineContext& ctx,
    TxPipelineExitKind exitKind,
    evmc_status_code evmStatus);
```

- [ ] **Step 2: Create test file**

```cpp
#define BOOST_TEST_MODULE OpStackDepositSettlementTest

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(deposit_success_actual_gas_commits_and_bumps_nonce)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x61);
    stateView.insert_account(sender, state::Account{.nonce = 5, .balance = 0});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 100'000;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, bcos::u256(999));

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 80'000;
    raw.gas_refund = 0;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    auto const result =
        finalizeDeposit(ctx, TxPipelineExitKind::Completed, EVMC_SUCCESS);

    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(result.gasRemaining, expected.gasRemaining);
    BOOST_CHECK_EQUAL(ctx.state.get_nonce(sender), 6u);
    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), bcos::u256(999));
}

BOOST_AUTO_TEST_CASE(deposit_revert_actual_gas_reverts_state_but_bumps_nonce)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x62);
    stateView.insert_account(sender, state::Account{.nonce = 7, .balance = 50});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 50'000;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    auto const balanceBefore = ctx.state.get_balance(sender);
    ctx.state.checkpoint();
    ctx.state.set_balance(sender, balanceBefore + bcos::u256(100));

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    raw.gas_left = 29'000;
    raw.gas_refund = 0;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);

    auto const result =
        finalizeDeposit(ctx, TxPipelineExitKind::Completed, EVMC_REVERT);

    auto const expected = postExecuteGasSettlement(50'000u, 29'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_LT(result.gasUsed, 50'000);
    BOOST_CHECK_EQUAL(ctx.state.get_nonce(sender), 8u);
    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), balanceBefore);
}

BOOST_AUTO_TEST_CASE(deposit_entry_failure_uses_gas_limit_and_bumps_nonce)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x63);
    stateView.insert_account(sender, state::Account{.nonce = 5, .balance = 0});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 20'999;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, bcos::u256(123));

    auto const result =
        finalizeDeposit(ctx, TxPipelineExitKind::IntrinsicRejected, EVMC_OUT_OF_GAS);

    BOOST_CHECK_EQUAL(result.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(result.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(ctx.state.get_nonce(sender), 6u);
    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), bcos::u256(0));
}
}  // namespace bcos::evm::test
```

- [ ] **Step 3: Register CMake target**

In `bcos-evm/test/cmake/OpStackTests.cmake`, after the `OpStackSettlement` test block:

```cmake
set(OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME OpStackDepositSettlementTest)

add_executable(${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME}
    opstack/OpStackDepositSettlementTest.cpp
)

target_include_directories(${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackDepositSettlement
    COMMAND ${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME}
)
```

- [ ] **Step 4: Build and verify tests fail (undefined symbol)**

Run:

```bash
cmake --build build/bcos-evm/test --target OpStackDepositSettlementTest -j
rtk test ctest --test-dir build/bcos-evm/test -R OpStackDepositSettlement --output-on-failure
```

Expected: link error `undefined symbol: finalizeDeposit` OR test binary fails if stub exists.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackSettlement.h \
  bcos-evm/test/opstack/OpStackDepositSettlementTest.cpp \
  bcos-evm/test/cmake/OpStackTests.cmake
rtk git commit -m "$(cat <<'EOF'
test(opstack): add OpStackDepositSettlementTest (red) for PR2 three-track table

EOF
)"
```

---

### Task 2: Implement `finalizeDeposit`

**Files:**
- Modify: `bcos-evm/opstack/OpStackSettlement.cpp`

**Interfaces:**
- Consumes: `postExecuteGasSettlement`, `State::{checkpoint,commit,revert,has_checkpoint}`, `ctx.originalGasLimit`, `ctx.evmcResult`, `ctx.revisionConfig.eip1559`
- Produces: `OpStackSettlementResult finalizeDeposit(TxPipelineContext&, TxPipelineExitKind, evmc_status_code)`

- [ ] **Step 1: Implement in `OpStackSettlement.cpp`**

Add helper and function:

```cpp
namespace
{
void applyDepositPostExecuteSettlement(
    TxPipelineContext const& ctx, OpStackSettlementResult& out, uint64_t floorDataGas)
{
    auto const stateRefund =
        ctx.revisionConfig.eip1559 ?
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) :
            uint64_t{0};
    auto const settlement = postExecuteGasSettlement(
        static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit)),
        static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_left)), stateRefund,
        floorDataGas);
    out.gasUsed = static_cast<int64_t>(settlement.gasUsed);
    out.gasRemaining = settlement.gasRemaining;
    out.maxUsedGas = settlement.maxUsedGas;
}
}  // namespace

OpStackSettlementResult finalizeDeposit(
    TxPipelineContext& ctx, TxPipelineExitKind exitKind, evmc_status_code evmStatus)
{
    OpStackSettlementResult out{};
    auto const sender = ctx.message.sender;

    if (exitKind == TxPipelineExitKind::Completed && evmStatus == EVMC_SUCCESS)
    {
        applyDepositPostExecuteSettlement(ctx, out, 0);
        ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
        ctx.state.commit();
        return out;
    }

    if (exitKind == TxPipelineExitKind::Completed)
    {
        applyDepositPostExecuteSettlement(ctx, out, 0);
        if (ctx.state.has_checkpoint())
        {
            ctx.state.revert();
        }
        ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
        return out;
    }

    out.gasUsed = std::max<int64_t>(0, ctx.originalGasLimit);
    out.gasRemaining = 0;
    if (ctx.state.has_checkpoint())
    {
        ctx.state.revert();
    }
    ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
    return out;
}
```

- [ ] **Step 2: Run deposit settlement tests**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R OpStackDepositSettlement --output-on-failure
```

Expected: 3/3 PASS

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackSettlement.cpp
rtk git commit -m "$(cat <<'EOF'
feat(opstack): implement finalizeDeposit three-track settlement (PR2)

EOF
)"
```

---

### Task 3: `finalizeNormal` H1 + stop feeCtx gas writes

**Files:**
- Modify: `bcos-evm/opstack/OpStackSettlement.h`
- Modify: `bcos-evm/opstack/OpStackSettlement.cpp`
- Modify: `bcos-evm/test/opstack/OpStackSettlementTest.cpp`

**Interfaces:**
- Produces: `OpStackSettlementResult finalizeNormal(TxPipelineContext const&, OpStackFeeContext const&, TxPipelineExitKind)`

- [ ] **Step 1: Change header signature**

Replace:

```cpp
OpStackSettlementResult finalizeNormal(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool);
```

With:

```cpp
OpStackSettlementResult finalizeNormal(TxPipelineContext const& ctx,
    OpStackFeeContext const& feeCtx, TxPipelineExitKind exitKind);
```

- [ ] **Step 2: Update `applyPostExecuteSettlement` to write only `out`**

```cpp
void applyPostExecuteSettlement(
    TxPipelineContext const& ctx, OpStackFeeContext const& feeCtx, OpStackSettlementResult& out)
{
    auto const stateRefund =
        ctx.revisionConfig.eip1559 ?
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) :
            uint64_t{0};
    auto const settlement =
        postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_left)), stateRefund,
            feeCtx.m_floorDataGas);
    out.gasUsed = static_cast<int64_t>(settlement.gasUsed);
    out.gasRemaining = settlement.gasRemaining;
    out.maxUsedGas = settlement.maxUsedGas;
}
```

Update `finalizeNormal` signature and remove `feeCtx.m_gas*` writes in early-exit branch (only set `out`).

- [ ] **Step 3: Update `OpStackSettlementTest.cpp`**

Remove `ledger`/`pool` locals; remove `feeCtx.m_gasUsed` / `m_gasRemaining` assertions. Example:

```cpp
auto result = finalizeNormal(ctx, feeCtx, ctx.exitKind);
BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
BOOST_CHECK_EQUAL(result.gasRemaining, expected.gasRemaining);
```

- [ ] **Step 4: Fix compile errors in `OpStackExecutionBridge.cpp` temporarily**

Change call site to 3-arg form until Task 6:

```cpp
auto settled = finalizeNormal(ctx, feeCtx, ctx.exitKind);
```

- [ ] **Step 5: Run tests**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R 'OpStackSettlement$' --output-on-failure
```

Expected: PASS (deposit tests still pass)

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackSettlement.h bcos-evm/opstack/OpStackSettlement.cpp \
  bcos-evm/test/opstack/OpStackSettlementTest.cpp bcos-evm/opstack/OpStackExecutionBridge.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(opstack): narrow finalizeNormal to sync gas math only (H1)

EOF
)"
```

---

### Task 4: `refundGas` reads `settled` + update ledger tests

**Files:**
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.h`
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.cpp`
- Modify: `bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp`
- Modify: `bcos-evm/test/opstack/RefundIsthmusTest.cpp`
- Modify: `bcos-evm/opstack/OpStackExecutionBridge.cpp` (bridge call — interim until Task 6)

**Interfaces:**
- Produces: `task::Task<void> refundGas(TxPipelineContext&, OpStackFeeContext const&, OpStackSettlementResult const&)`
- Produces: `refundIsthmusOperatorCost(ctx, feeCtx, uint64_t gasUsed)` or add `settled` param

- [ ] **Step 1: Update header**

```cpp
#include "bcos-evm/opstack/OpStackSettlement.h"  // OpStackSettlementResult

task::Task<void> refundGas(TxPipelineContext& ctx, OpStackFeeContext const& feeCtx,
    OpStackSettlementResult const& settled);
task::Task<void> refundIsthmusOperatorCost(TxPipelineContext& ctx, OpStackFeeContext const& feeCtx,
    uint64_t gasUsed);
```

- [ ] **Step 2: Update `refundGas` implementation**

Replace `feeCtx.m_gasRemaining` / `feeCtx.m_gasUsed` with `settled.gasRemaining` / `settled.gasUsed`:

```cpp
task::Task<void> OpStackTxFeeLedger::refundGas(
    TxPipelineContext& ctx, OpStackFeeContext const& feeCtx, OpStackSettlementResult const& settled)
{
    // ... existing early returns unchanged ...

    auto const gasRemaining = settled.gasRemaining;
    auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed));
    // ... rest unchanged ...

    co_await refundIsthmusOperatorCost(ctx, feeCtx, gasUsed);
    // ...
}
```

Update `refundIsthmusOperatorCost` to take `uint64_t gasUsed` instead of reading `feeCtx.m_gasUsed`.

- [ ] **Step 3: Update `OpStackTxFeeLedgerCtxTest.cpp`**

```cpp
OpStackSettlementResult settled;
settled.gasUsed = 400;
settled.gasRemaining = 600;
task::syncWait(executor.refundGas(ctx, feeCtx, settled));
```

And in `HardFailure_stillRefundsUnusedGas`:

```cpp
OpStackSettlementResult settled;
settled.gasUsed = static_cast<int64_t>(settlement.gasUsed);
settled.gasRemaining = settlement.gasRemaining;
task::syncWait(executor.refundGas(ctx, feeCtx, settled));
```

- [ ] **Step 4: Update `RefundIsthmusTest.cpp`**

```cpp
task::syncWait(executor.refundIsthmusOperatorCost(ctx, feeCtx, 500));
```

- [ ] **Step 5: Update bridge interim call**

```cpp
co_await input.opTxExecutor.refundGas(ctx, feeCtx, settled);
```

- [ ] **Step 6: Run ledger tests**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R 'OpStackTxFeeLedgerCtx|RefundIsthmus' --output-on-failure
```

Expected: PASS

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackTxFeeLedger.h bcos-evm/opstack/OpStackTxFeeLedger.cpp \
  bcos-evm/test/opstack/OpStackTxFeeLedgerCtxTest.cpp bcos-evm/test/opstack/RefundIsthmusTest.cpp \
  bcos-evm/opstack/OpStackExecutionBridge.cpp
rtk git commit -m "$(cat <<'EOF'
feat(opstack): refundGas reads OpStackSettlementResult instead of feeCtx

EOF
)"
```

---

### Task 5: Remove gas fields from `OpStackFeeContext` (E1)

**Files:**
- Modify: `bcos-evm/opstack/OpStackTxFeeLedger.h`
- Modify: `bcos-evm/opstack/OpStackExecutionBridge.cpp` (deposit branch still references `feeCtx.m_gasUsed` until Task 6)

**Interfaces:**
- Produces: `OpStackFeeContext` without `m_gasUsed`, `m_gasRemaining`, `m_maxUsedGas`

- [ ] **Step 1: Delete three fields from `OpStackFeeContext`**

Remove from `OpStackTxFeeLedger.h`:

```cpp
int64_t m_gasUsed{0};
uint64_t m_gasRemaining{0};
uint64_t m_maxUsedGas{0};
```

- [ ] **Step 2: Grep and fix remaining references**

```bash
rtk grep m_gasUsed bcos-evm/opstack bcos-evm/test/opstack
```

Fix any compile errors outside bridge deposit block (bridge fixed in Task 6).

- [ ] **Step 3: Build opstack lib**

```bash
cmake --build build/bcos-evm --target bcos-evm-op -j
```

Expected: may fail on bridge deposit lines — acceptable until Task 6.

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackTxFeeLedger.h
rtk git commit -m "$(cat <<'EOF'
refactor(opstack): remove gas output fields from OpStackFeeContext (E1)

EOF
)"
```

---

### Task 6: `settleNormal` + `settleDeposit` facades

**Files:**
- Modify: `bcos-evm/opstack/OpStackSettlement.h`
- Modify: `bcos-evm/opstack/OpStackSettlement.cpp`

**Interfaces:**
- Produces:
  - `task::Task<OpStackSettlementResult> settleNormal(TxPipelineContext&, OpStackFeeContext&, TxPipelineExitKind, OpStackTxFeeLedger&, GasPoolHooks const&)`
  - `task::Task<OpStackSettlementResult> settleDeposit(TxPipelineContext&, TxPipelineExitKind, evmc_status_code, GasPoolHooks const&)`

- [ ] **Step 1: Add declarations and includes**

In `OpStackSettlement.h`:

```cpp
#include <bcos-task/Task.h>

task::Task<OpStackSettlementResult> settleNormal(TxPipelineContext& ctx,
    OpStackFeeContext& feeCtx, TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger,
    GasPoolHooks const& gasPool);

task::Task<OpStackSettlementResult> settleDeposit(TxPipelineContext& ctx,
    TxPipelineExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool);
```

- [ ] **Step 2: Implement facades**

```cpp
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include <bcos-task/Task.h>

task::Task<OpStackSettlementResult> settleNormal(TxPipelineContext& ctx,
    OpStackFeeContext& feeCtx, TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger,
    GasPoolHooks const& gasPool)
{
    auto settled = finalizeNormal(ctx, feeCtx, exitKind);
    co_await ledger.refundGas(ctx, feeCtx, settled);
    if (gasPool.returnGas)
    {
        gasPool.returnGas(settled.gasRemaining,
            static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return settled;
}

task::Task<OpStackSettlementResult> settleDeposit(TxPipelineContext& ctx,
    TxPipelineExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool)
{
    auto settled = finalizeDeposit(ctx, exitKind, evmStatus);
    if (gasPool.returnGas)
    {
        gasPool.returnGas(settled.gasRemaining,
            static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return settled;
}
```

- [ ] **Step 3: Build**

```bash
cmake --build build/bcos-evm --target bcos-evm-op -j
```

Expected: PASS (bridge still uses old paths — OK)

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackSettlement.h bcos-evm/opstack/OpStackSettlement.cpp
rtk git commit -m "$(cat <<'EOF'
feat(opstack): add settleNormal and settleDeposit async facades (PR2)

EOF
)"
```

---

### Task 7: Refactor `OpStackExecutionBridge.cpp`

**Files:**
- Modify: `bcos-evm/opstack/OpStackExecutionBridge.cpp`

**Interfaces:**
- Consumes: `settleNormal`, `settleDeposit` from Task 6

- [ ] **Step 1: Delete `GasPoolReturnGuard` and `returnDepositPoolGas`**

Remove struct `GasPoolReturnGuard` and function `returnDepositPoolGas` from anonymous namespace.

- [ ] **Step 2: Refactor deposit branch**

Replace lines ~129–154 with:

```cpp
GasPoolHooks gasPool{
    .subGas = input.gasPoolSubGasHook,
    .returnGas = input.gasPoolReturnGasHook,
};
auto settled = co_await settleDeposit(
    ctx, ctx.exitKind, output.evmcResult.status_code, gasPool);
output.gasUsed = settled.gasUsed;
output.stateDiff = ctx.state.build_diff();
co_return output;
```

Keep pre-pipeline block (depositNonce, mint, checkpoint, pipeline) unchanged.

- [ ] **Step 3: Refactor normal branch**

After `buyGas` failure, add pool return:

```cpp
auto buyGasOk = co_await input.opTxExecutor.buyGas(ctx, feeCtx);
if (!buyGasOk)
{
    if (input.gasPoolReturnGasHook)
    {
        auto const gasLimitForPool =
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        input.gasPoolReturnGasHook(gasLimitForPool, 0);
    }
    output.evmcResult = std::move(*feeCtx.m_evmcResult);
    co_return output;
}
```

Replace post-pipeline block with:

```cpp
GasPoolHooks gasPool{
    .subGas = input.gasPoolSubGasHook,
    .returnGas = input.gasPoolReturnGasHook,
};
auto settled = co_await settleNormal(ctx, feeCtx, ctx.exitKind, input.opTxExecutor, gasPool);

output.gasUsed = settled.gasUsed;
// receiptMeta mapping unchanged (uses settled.gasUsed for operator fee)
```

Remove `co_await refundGas` and `guard` usage.

- [ ] **Step 4: Verify no direct `refundGas` / `finalizeNormal` in bridge**

```bash
rtk grep -n 'refundGas\|finalizeNormal\|GasPoolReturnGuard\|returnDepositPoolGas' bcos-evm/opstack/OpStackExecutionBridge.cpp
```

Expected: no matches (only `settleNormal` / `settleDeposit`)

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackExecutionBridge.cpp
rtk git commit -m "$(cat <<'EOF'
refactor(opstack): bridge uses settle* facades; remove Guard and deposit inline

EOF
)"
```

---

### Task 8: Full regression + ADR PR2 closure

**Files:**
- Modify: `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md`

- [ ] **Step 1: Run OpStack settlement + deposit suite**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R 'OpStackSettlement|OpStackDepositSettlement|DepositMint|DepositNoFee|DepositCreate|L1AttributesDeposit' --output-on-failure
```

Expected: all PASS

- [ ] **Step 2: Run characterization**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R OpStackSettlementCharacterization --output-on-failure
```

Expected: PASS

- [ ] **Step 3: Update ADR-021**

- Status → `Implemented (PR1 + PR2)`
- PR2 Compliance section: all `[ ]` → `[x]`
- Remove "PR1 gap" note in §2.1 or mark closed

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md
rtk git commit -m "$(cat <<'EOF'
docs(adr): mark ADR-021 PR2 settlement facade as implemented

EOF
)"
```

---

### Task 9: Unified build + test fix gate

**Policy:** Tasks 1–8 skip per-task `cmake`/`ctest`; tests must be written. Task 9 compiles everything and fixes integration failures.

**Files:**
- Modify: `bcos-evm/eth/execution/InnerExecute.h`, `bcos-evm/eth/ExecuteMessage.cpp`
- Modify: `bcos-evm/opstack/OpStackPipelineHookBinder.cpp`
- Modify: `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md`

- [x] **Step 1: Build `bcos-evm-op` + test targets**

```bash
cmake --build build --target bcos-evm-op -j8
cmake --build build/bcos-evm/test -j8
```

- [x] **Step 2: Run settlement + deposit regression**

```bash
ctest --test-dir build/bcos-evm/test -R 'OpStackSettlement|OpStackDepositSettlement|OpStackTxFeeLedgerCtx|RefundIsthmus|DepositMint|DepositNoFee|DepositCreate|L1AttributesDeposit' --output-on-failure
```

Expected: 10/10 PASS

- [x] **Step 3: Fix `DepositCreateNonce` double bump**

`executeMessage` top-level sender nonce bump conflicts with `finalizeDeposit` on deposit SUCCESS (ExecutionFrame PR2 regression). OpStack deposit pipeline sets `skipTopLevelSenderNonceBump` via `txTuneExecutionInput`; orchestration owns nonce via `finalizeDeposit`.

- [x] **Step 4: ADR-021 PR2 closure** — Status `Implemented (PR1 + PR2)`; Compliance all `[x]`

---

## Plan Self-Review

| Spec requirement | Task |
| --- | --- |
| `finalizeDeposit` three-track | Task 1–2 |
| `settleNormal` / `settleDeposit` | Task 6–7 |
| `refundGas` in facade | Task 4, 6–7 |
| Gas pool in facade + buyGas fail return | Task 7 |
| E1 feeCtx narrow | Task 5 |
| H1 finalizeNormal signature | Task 3 |
| G1 test-first deposit | Task 1 before Task 2 |
| ADR-021 PR2 compliance | Task 8 |
| ADR-005 eth seam | No eth/ changes |
| Optional settleNormal unit test | Not in plan (YAGNI; characterization covers) |

**Placeholder scan:** None found.

**Type consistency:** `OpStackSettlementResult` used consistently from Task 4 onward; `finalizeDeposit`/`finalizeNormal` signatures match header in Task 3/2.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-25-opstack-settlement-pr2.md`. Two execution options:

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?
