# OpStack settle* Async Layer Unit Tests — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close ADR-021 async-layer test gap by adding 14 direct unit tests for `settleNormal` / `settleDeposit` wiring (no production code changes).

**Architecture:** New `OpStackSettleAsyncTest.cpp` uses `GasPoolSpy` to capture `gasPool.returnGas` args and balance-oracle comparison to prove `refundGas` ran inside `settleNormal`. Sync-layer tests (`OpStackSettlementTest`, `OpStackDepositSettlementTest`) stay unchanged.

**Tech Stack:** C++17, Boost.Test (included), `bcos-task` + `task::syncWait`, `bcos-evm-op`, `InMemoryEvmStateReader`.

**Spec:** `docs/superpowers/specs/2026-06-25-opstack-settle-async-test-design.md`

## Global Constraints

- **Zero production changes** — do not edit `bcos-evm/opstack/*.cpp` or `*.h` except test files and docs listed below.
- Use `rtk` prefix on shell commands per project convention.
- Test file links `bcos-evm-op` (settle* already implemented in PR2).
- Do not duplicate `finalize*` math assertions beyond oracle cross-check.
- Do not duplicate `refundGas` routing detail covered by `OpStackTxFeeLedgerCtxTest`.
- Register new target in `bcos-evm/test/cmake/OpStackTests.cmake` immediately after `OpStackDepositSettlement` block (~L344).

## File Map

| File | Action | Responsibility |
| --- | --- | --- |
| `bcos-evm/test/opstack/helpers/OpStackSettleTestHelpers.h` | Create | `GasPoolSpy`, ctx builders, spy/balance assert helpers |
| `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp` | Create | 14 async matrix cases |
| `bcos-evm/test/cmake/OpStackTests.cmake` | Modify | Register `OpStackSettleAsyncTest` |
| `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md` | Modify §7.4 | Mark settle* tests Implemented |
| `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md` | Modify | Add async test coverage row |

---

### Task 1: Test harness + CMake + first `settleNormal` case (N1)

**Files:**
- Create: `bcos-evm/test/opstack/helpers/OpStackSettleTestHelpers.h`
- Create: `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp` (N1 only)
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake` (after L344)

**Interfaces:**
- Consumes: `settleNormal`, `finalizeNormal`, `OpStackTxFeeLedger::buyGas` / `refundGas`, `GasPoolHooks`
- Produces: `GasPoolSpy`, `makeNormalSettleFixture`, `assertGasPoolMatchesSettled`, `assertSettleNormalBalancesMatchManualRefund`

- [ ] **Step 1: Create helpers header**

Create `bcos-evm/test/opstack/helpers/OpStackSettleTestHelpers.h`:

```cpp
#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "state/InMemoryEvmStateReader.h"
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
struct GasPoolSpy
{
    int returnGasCallCount{0};
    uint64_t lastRemaining{0};
    uint64_t lastUsed{0};

    GasPoolHooks hooks()
    {
        GasPoolHooks out{};
        out.returnGas = [this](uint64_t gasRemaining, uint64_t gasUsed) {
            ++returnGasCallCount;
            lastRemaining = gasRemaining;
            lastUsed = gasUsed;
        };
        return out;
    }
};

inline evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

struct NormalSettleFixture
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_address sender{};
    evmc_address coinbase{};
    TxPipelineContext ctx;
    OpStackFeeContext feeCtx;
    OpStackTxFeeLedger ledger;
    GasPoolSpy spy;

    NormalSettleFixture(int64_t gasLimit, TxPipelineExitKind exitKind, int64_t gasLeft,
        evmc_status_code status = EVMC_SUCCESS, int64_t gasRefund = 0)
      : sender(addressFromLastByte(0x01)),
        coinbase(addressFromLastByte(0x02)),
        ctx(stateView, evmc_message{}, bcos::evm_standard::makeIsthmusRevisionConfig(), bcos::u256(0))
    {
        stateView.insert_account(sender, state::Account{.balance = u256(500'000)});

        evmc_message msg{};
        msg.sender = sender;
        msg.gas = gasLimit;
        ctx.message = msg;

        evmc_result raw{};
        raw.status_code = status;
        raw.gas_left = gasLeft;
        raw.gas_refund = gasRefund;
        ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
        ctx.exitKind = exitKind;

        feeCtx.m_gasTipCap = 5;
        feeCtx.m_gasFeeCap = 10;
        feeCtx.m_hasGasFeeCap = true;
        feeCtx.m_blockInfo.timestamp = 1;
        feeCtx.m_blockInfo.baseFee = 2;
        feeCtx.m_blockInfo.coinbase = coinbase;
        feeCtx.m_rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};

        ledger.m_l1CostFunc = [](RollupCostData const&, uint64_t) { return u256(100); };
        ledger.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 10); };
    }

    void buyGas()
    {
        auto ok = task::syncWait(ledger.buyGas(ctx, feeCtx));
        BOOST_REQUIRE(ok);
    }
};

inline void assertGasPoolMatchesSettled(
    GasPoolSpy const& spy, OpStackSettlementResult const& settled)
{
    BOOST_REQUIRE_EQUAL(spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(spy.lastRemaining, settled.gasRemaining);
    BOOST_CHECK_EQUAL(spy.lastUsed, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
}

inline void assertSettleNormalBalancesMatchManualRefund(NormalSettleFixture const& fixture,
    OpStackSettlementResult const& settled)
{
    state::test::InMemoryEvmStateReader oracleView;
    oracleView.insert_account(fixture.sender, state::Account{
        .balance = fixture.stateView.get_balance(fixture.sender)});

    evmc_message msg = fixture.ctx.message;
    TxPipelineContext oracleCtx{
        oracleView, msg, fixture.ctx.revisionConfig, fixture.ctx.gasPrice};
    oracleCtx.evmcResult = fixture.ctx.evmcResult;
    oracleCtx.exitKind = fixture.ctx.exitKind;

    OpStackFeeContext oracleFee = fixture.feeCtx;
    OpStackTxFeeLedger oracleLedger = fixture.ledger;
    BOOST_REQUIRE(task::syncWait(oracleLedger.buyGas(oracleCtx, oracleFee)));

    auto const oracleSettled =
        finalizeNormal(oracleCtx, oracleFee, oracleCtx.exitKind);
    BOOST_CHECK_EQUAL(oracleSettled.gasUsed, settled.gasUsed);
    BOOST_CHECK_EQUAL(oracleSettled.gasRemaining, settled.gasRemaining);

    task::syncWait(oracleLedger.refundGas(oracleCtx, oracleFee, oracleSettled));

    BOOST_CHECK_EQUAL(oracleView.get_balance(fixture.sender),
        fixture.stateView.get_balance(fixture.sender));
    BOOST_CHECK_EQUAL(oracleView.get_balance(fixture.coinbase),
        fixture.stateView.get_balance(fixture.coinbase));
    BOOST_CHECK_EQUAL(oracleView.get_balance(OP_BASE_FEE_RECIPIENT),
        fixture.stateView.get_balance(OP_BASE_FEE_RECIPIENT));
    BOOST_CHECK_EQUAL(oracleView.get_balance(OP_L1_FEE_RECIPIENT),
        fixture.stateView.get_balance(OP_L1_FEE_RECIPIENT));
    BOOST_CHECK_EQUAL(oracleView.get_balance(OP_OPERATOR_FEE_RECIPIENT),
        fixture.stateView.get_balance(OP_OPERATOR_FEE_RECIPIENT));
}

struct DepositSettleFixture
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_address sender{};
    TxPipelineContext ctx;
    GasPoolSpy spy;

    DepositSettleFixture(int64_t gasLimit, TxPipelineExitKind exitKind,
        evmc_status_code evmStatus, int64_t gasLeft)
      : sender(addressFromLastByte(0x61)),
        ctx(stateView, evmc_message{}, bcos::evm_standard::makeIsthmusRevisionConfig(), bcos::u256(0))
    {
        stateView.insert_account(sender, state::Account{.balance = 0, .nonce = 5});

        evmc_message msg{};
        msg.sender = sender;
        msg.gas = gasLimit;
        ctx.message = msg;

        ctx.state.checkpoint();
        ctx.state.set_balance(sender, bcos::u256(999));

        evmc_result raw{};
        raw.status_code = evmStatus;
        raw.gas_left = gasLeft;
        raw.gas_refund = 0;
        ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
        ctx.exitKind = exitKind;
    }
};

inline void assertGasPoolMatchesFinalizeDeposit(GasPoolSpy const& spy,
    TxPipelineContext const& ctx, TxPipelineExitKind exitKind, evmc_status_code evmStatus)
{
    state::test::InMemoryEvmStateReader oracleView;
    auto msg = ctx.message;
    TxPipelineContext oracleCtx{
        oracleView, msg, ctx.revisionConfig, ctx.gasPrice};
    oracleCtx.evmcResult = ctx.evmcResult;
    oracleCtx.exitKind = exitKind;
    oracleView.insert_account(ctx.message.sender,
        state::Account{.balance = ctx.state.get_balance(ctx.message.sender),
            .nonce = ctx.state.get_nonce(ctx.message.sender)});
    if (ctx.state.has_checkpoint())
    {
        oracleCtx.state.checkpoint();
    }

    auto const oracleSettled = finalizeDeposit(oracleCtx, exitKind, evmStatus);
    assertGasPoolMatchesSettled(spy, oracleSettled);
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: Create test file with N1**

Create `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp`:

```cpp
#define BOOST_TEST_MODULE OpStackSettleAsyncTest

#include "opstack/helpers/OpStackSettleTestHelpers.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(settle_normal_completed_wires_refund_and_gas_pool)
{
    NormalSettleFixture fixture(100'000, TxPipelineExitKind::Completed, 80'000);
    fixture.buyGas();

    auto const settled = task::syncWait(settleNormal(fixture.ctx, fixture.feeCtx,
        fixture.ctx.exitKind, fixture.ledger, fixture.spy.hooks()));

    auto const oracle =
        finalizeNormal(fixture.ctx, fixture.feeCtx, fixture.ctx.exitKind);
    BOOST_CHECK_EQUAL(settled.gasUsed, oracle.gasUsed);
    BOOST_CHECK_EQUAL(settled.gasRemaining, oracle.gasRemaining);

    assertGasPoolMatchesSettled(fixture.spy, settled);
    assertSettleNormalBalancesMatchManualRefund(fixture, settled);
}
}  // namespace bcos::evm::test
```

- [ ] **Step 3: Register CMake target**

In `bcos-evm/test/cmake/OpStackTests.cmake`, after the `OpStackDepositSettlement` test block (~L344), add:

```cmake
set(OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME OpStackSettleAsyncTest)

add_executable(${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME}
    opstack/OpStackSettleAsyncTest.cpp
)

target_include_directories(${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackSettleAsync
    COMMAND ${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME}
)
```

- [ ] **Step 4: Build and run N1**

From repo build dir (adjust path to your build tree):

```bash
rtk cmake --build <build-dir> --target OpStackSettleAsyncTest -j
rtk ctest --test-dir <build-dir> -R OpStackSettleAsync --output-on-failure
```

Expected: PASS (1 test case).

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/opstack/helpers/OpStackSettleTestHelpers.h \
  bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp \
  bcos-evm/test/cmake/OpStackTests.cmake
rtk git commit -m "$(cat <<'EOF'
test(opstack): Add settle async harness and completed-path wiring test

Introduce GasPoolSpy helpers and N1 settleNormal case per ADR-021 async
layer test design.
EOF
)"
```

---

### Task 2: Remaining `settleNormal` cases (N2–N8)

**Files:**
- Modify: `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp`

**Interfaces:**
- Consumes: Task 1 helpers (`NormalSettleFixture`, `GasPoolSpy`, assert helpers)
- Produces: 7 additional `BOOST_AUTO_TEST_CASE`s

- [ ] **Step 1: Add N2 — rules rejected**

```cpp
BOOST_AUTO_TEST_CASE(settle_normal_rules_rejected_wires_partial_refund)
{
    NormalSettleFixture fixture(
        100'000, TxPipelineExitKind::RulesRejected, 60'000, EVMC_REVERT);
    fixture.buyGas();

    auto const settled = task::syncWait(settleNormal(fixture.ctx, fixture.feeCtx,
        fixture.ctx.exitKind, fixture.ledger, fixture.spy.hooks()));

    BOOST_CHECK_GT(settled.gasUsed, int64_t{0});
    assertGasPoolMatchesSettled(fixture.spy, settled);
    assertSettleNormalBalancesMatchManualRefund(fixture, settled);
}
```

- [ ] **Step 2: Add N3 — exception handled**

```cpp
BOOST_AUTO_TEST_CASE(settle_normal_exception_handled_wires_partial_refund)
{
    NormalSettleFixture fixture(
        100'000, TxPipelineExitKind::ExceptionHandled, 60'000, EVMC_REVERT);
    fixture.buyGas();

    auto const settled = task::syncWait(settleNormal(fixture.ctx, fixture.feeCtx,
        fixture.ctx.exitKind, fixture.ledger, fixture.spy.hooks()));

    BOOST_CHECK_GT(settled.gasUsed, int64_t{0});
    assertGasPoolMatchesSettled(fixture.spy, settled);
    assertSettleNormalBalancesMatchManualRefund(fixture, settled);
}
```

- [ ] **Step 3: Add N4 — intrinsic reject**

```cpp
BOOST_AUTO_TEST_CASE(settle_normal_intrinsic_reject_return_gas_full_limit)
{
    NormalSettleFixture fixture(50'000, TxPipelineExitKind::IntrinsicRejected, 0);
    fixture.buyGas();

    auto const settled = task::syncWait(settleNormal(fixture.ctx, fixture.feeCtx,
        fixture.ctx.exitKind, fixture.ledger, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, int64_t{0});
    BOOST_CHECK_EQUAL(settled.gasRemaining, 50'000u);
    assertGasPoolMatchesSettled(fixture.spy, settled);
}
```

- [ ] **Step 4: Add N5 — gas afford reject**

```cpp
BOOST_AUTO_TEST_CASE(settle_normal_gas_afford_reject_return_gas_full_limit)
{
    NormalSettleFixture fixture(50'000, TxPipelineExitKind::GasAffordRejected, 0);
    fixture.buyGas();

    auto const settled = task::syncWait(settleNormal(fixture.ctx, fixture.feeCtx,
        fixture.ctx.exitKind, fixture.ledger, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, int64_t{0});
    BOOST_CHECK_EQUAL(settled.gasRemaining, 50'000u);
    assertGasPoolMatchesSettled(fixture.spy, settled);
}
```

- [ ] **Step 5: Add N6 — null returnGas hook**

```cpp
BOOST_AUTO_TEST_CASE(settle_normal_null_return_gas_hook_no_crash)
{
    NormalSettleFixture fixture(100'000, TxPipelineExitKind::Completed, 80'000);
    fixture.buyGas();

    GasPoolHooks emptyHooks{};
    auto const settled = task::syncWait(settleNormal(
        fixture.ctx, fixture.feeCtx, fixture.ctx.exitKind, fixture.ledger, emptyHooks));

    auto const oracle =
        finalizeNormal(fixture.ctx, fixture.feeCtx, fixture.ctx.exitKind);
    BOOST_CHECK_EQUAL(settled.gasUsed, oracle.gasUsed);
    BOOST_CHECK_EQUAL(settled.gasRemaining, oracle.gasRemaining);
}
```

- [ ] **Step 6: Add N7 — call frame skips refund routing**

```cpp
BOOST_AUTO_TEST_CASE(settle_normal_call_frame_skips_refund_routing)
{
    NormalSettleFixture fixture(100'000, TxPipelineExitKind::Completed, 80'000);
    fixture.feeCtx.m_call = true;
    fixture.feeCtx.m_skipTransactionChecks = true;
    fixture.feeCtx.m_noBaseFee = true;
    fixture.feeCtx.m_gasTipCap = 0;
    fixture.feeCtx.m_gasFeeCap = 0;
    fixture.buyGas();

    auto const senderBefore = fixture.stateView.get_balance(fixture.sender);
    auto const settled = task::syncWait(settleNormal(fixture.ctx, fixture.feeCtx,
        fixture.ctx.exitKind, fixture.ledger, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(fixture.stateView.get_balance(fixture.sender), senderBefore);
    assertGasPoolMatchesSettled(fixture.spy, settled);
}
```

Note: after `buyGas` with tip/fee=0, sender balance may still change from L1/operator precharge — N7 asserts **no additional refund delta** beyond post-buyGas state. Capture `senderAfterBuyGas` before settle:

```cpp
    auto const senderAfterBuyGas = fixture.stateView.get_balance(fixture.sender);
    auto const settled = task::syncWait(settleNormal(...));
    BOOST_CHECK_EQUAL(fixture.stateView.get_balance(fixture.sender), senderAfterBuyGas);
```

- [ ] **Step 7: Add N8 — deposit flag skips refund**

```cpp
BOOST_AUTO_TEST_CASE(settle_normal_deposit_flag_skips_refund_routing)
{
    NormalSettleFixture fixture(100'000, TxPipelineExitKind::Completed, 80'000);
    fixture.feeCtx.m_isDepositTx = true;
    fixture.buyGas();

    auto const senderAfterBuyGas = fixture.stateView.get_balance(fixture.sender);
    auto const settled = task::syncWait(settleNormal(fixture.ctx, fixture.feeCtx,
        fixture.ctx.exitKind, fixture.ledger, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(fixture.stateView.get_balance(fixture.sender), senderAfterBuyGas);
    assertGasPoolMatchesSettled(fixture.spy, settled);
}
```

- [ ] **Step 8: Run settleNormal suite**

```bash
rtk ctest --test-dir <build-dir> -R OpStackSettleAsync --output-on-failure
```

Expected: PASS (8 test cases).

- [ ] **Step 9: Commit**

```bash
rtk git add bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp
rtk git commit -m "$(cat <<'EOF'
test(opstack): Cover settleNormal async matrix N2-N8

Add early-exit, null hook, and refund-skip edge cases for ADR-021 wiring.
EOF
)"
```

---

### Task 3: `settleDeposit` cases (D1–D6)

**Files:**
- Modify: `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp`
- Modify: `bcos-evm/test/opstack/helpers/OpStackSettleTestHelpers.h` (fix `assertGasPoolMatchesFinalizeDeposit` if oracle nonce/checkpoint setup needs sender seed from fixture)

**Interfaces:**
- Consumes: `settleDeposit`, `DepositSettleFixture`, `GasPoolSpy`
- Produces: 6 deposit async cases

- [ ] **Step 1: Add D1 — success**

```cpp
BOOST_AUTO_TEST_CASE(settle_deposit_success_commits_and_returns_gas_pool)
{
    DepositSettleFixture fixture(
        100'000, TxPipelineExitKind::Completed, EVMC_SUCCESS, 80'000);

    auto const settled = task::syncWait(settleDeposit(
        fixture.ctx, fixture.ctx.exitKind, EVMC_SUCCESS, fixture.spy.hooks()));

    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(settled.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(settled.gasRemaining, expected.gasRemaining);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), bcos::u256(999));
    assertGasPoolMatchesSettled(fixture.spy, settled);
}
```

- [ ] **Step 2: Add D2 — revert**

```cpp
BOOST_AUTO_TEST_CASE(settle_deposit_revert_returns_actual_gas)
{
    DepositSettleFixture fixture(
        50'000, TxPipelineExitKind::Completed, EVMC_REVERT, 29'000);

    auto const settled = task::syncWait(settleDeposit(
        fixture.ctx, fixture.ctx.exitKind, EVMC_REVERT, fixture.spy.hooks()));

    BOOST_CHECK_LT(settled.gasUsed, 50'000);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), bcos::u256(0));
    assertGasPoolMatchesSettled(fixture.spy, settled);
}
```

- [ ] **Step 3: Add D3 — intrinsic entry failure**

```cpp
BOOST_AUTO_TEST_CASE(settle_deposit_entry_failure_uses_gas_limit)
{
    DepositSettleFixture fixture(20'999, TxPipelineExitKind::IntrinsicRejected, EVMC_OUT_OF_GAS, 0);

    auto const settled = task::syncWait(settleDeposit(fixture.ctx,
        TxPipelineExitKind::IntrinsicRejected, EVMC_OUT_OF_GAS, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(settled.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), bcos::u256(0));
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 20'999u);
}
```

- [ ] **Step 4: Add D4 — gas afford entry failure**

```cpp
BOOST_AUTO_TEST_CASE(settle_deposit_gas_afford_reject_entry_failure)
{
    DepositSettleFixture fixture(20'999, TxPipelineExitKind::GasAffordRejected, EVMC_OUT_OF_GAS, 0);

    auto const settled = task::syncWait(settleDeposit(fixture.ctx,
        TxPipelineExitKind::GasAffordRejected, EVMC_OUT_OF_GAS, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(settled.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 20'999u);
}
```

- [ ] **Step 5: Add D5 — null returnGas hook**

```cpp
BOOST_AUTO_TEST_CASE(settle_deposit_null_return_gas_hook_no_crash)
{
    DepositSettleFixture fixture(
        100'000, TxPipelineExitKind::Completed, EVMC_SUCCESS, 80'000);

    GasPoolHooks emptyHooks{};
    auto const settled = task::syncWait(settleDeposit(
        fixture.ctx, fixture.ctx.exitKind, EVMC_SUCCESS, emptyHooks));

    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(settled.gasUsed, static_cast<int64_t>(expected.gasUsed));
}
```

- [ ] **Step 6: Add D6 — exception handled entry failure**

```cpp
BOOST_AUTO_TEST_CASE(settle_deposit_exception_handled_entry_failure)
{
    DepositSettleFixture fixture(20'999, TxPipelineExitKind::ExceptionHandled, EVMC_OUT_OF_GAS, 0);

    auto const settled = task::syncWait(settleDeposit(fixture.ctx,
        TxPipelineExitKind::ExceptionHandled, EVMC_OUT_OF_GAS, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(settled.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 20'999u);
}
```

- [ ] **Step 7: Run full async suite**

```bash
rtk ctest --test-dir <build-dir> -R OpStackSettleAsync --output-on-failure
```

Expected: PASS (14 test cases).

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp \
  bcos-evm/test/opstack/helpers/OpStackSettleTestHelpers.h
rtk git commit -m "$(cat <<'EOF'
test(opstack): Cover settleDeposit async matrix D1-D6

Complete ADR-021 async layer unit test matrix for deposit path wiring.
EOF
)"
```

---

### Task 4: Regression gate + documentation

**Files:**
- Modify: `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md` §7.4
- Modify: `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md`

**Interfaces:**
- Consumes: all 14 tests green
- Produces: updated doc status rows

- [ ] **Step 1: Run regression gate**

```bash
rtk ctest --test-dir <build-dir> -R 'OpStackSettleAsync|OpStackSettlement|OpStackDepositSettlement|OpStackTxFeeLedgerCtx|OpStackSettlementCharacterization' --output-on-failure
```

Expected: all PASS.

- [ ] **Step 2: Update PR2 spec §7.4**

In `docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md`, replace §7.4 heading/body:

```markdown
### 7.4 `settleNormal` / `settleDeposit` async unit tests (Implemented)

- `bcos-evm/test/opstack/OpStackSettleAsyncTest.cpp` — 14 cases (8 normal + 6 deposit)
- `GasPoolSpy` verifies `returnGas(settled.gasRemaining, settled.gasUsed)`
- Balance oracle verifies `refundGas` inside `settleNormal`
- See `docs/superpowers/specs/2026-06-25-opstack-settle-async-test-design.md`
```

- [ ] **Step 3: Update ADR-021 test table**

In `bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md`, add under §2.2 or a new **Test coverage** subsection:

```markdown
| Layer | Tests |
| --- | --- |
| Sync `finalizeNormal` | `OpStackSettlementTest` |
| Sync `finalizeDeposit` | `OpStackDepositSettlementTest` |
| Async `settleNormal` / `settleDeposit` | `OpStackSettleAsyncTest` (14 cases) |
| Fee ledger routing | `OpStackTxFeeLedgerCtxTest` |
| E2E oracle | `OpStackSettlementCharacterizationTest`, deposit integration tests |
```

- [ ] **Step 4: Update async test design spec status**

In `docs/superpowers/specs/2026-06-25-opstack-settle-async-test-design.md`, change `Status: Draft (pending user review)` → `Status: Implemented`.

- [ ] **Step 5: Commit docs**

```bash
rtk git add docs/superpowers/specs/2026-06-25-opstack-settlement-pr2-design.md \
  docs/superpowers/specs/2026-06-25-opstack-settle-async-test-design.md \
  bcos-evm/docs/adr/021-opstack-settlement-ctx-single-source.md
rtk git commit -m "$(cat <<'EOF'
docs(opstack): Mark settle* async unit tests implemented in ADR-021
EOF
)"
```

---

## Spec Coverage Checklist

| Spec requirement | Task |
| --- | --- |
| 14 async cases (N1–N8, D1–D6) | Tasks 1–3 |
| `GasPoolSpy` harness | Task 1 |
| Balance oracle for `refundGas` | Task 1 (N1–N3) |
| Zero production changes | All tasks |
| CMake `OpStackSettleAsync` | Task 1 |
| Regression gate (5 targets) | Task 4 |
| PR2 spec §7.4 update | Task 4 |
| ADR-021 test table | Task 4 |

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-25-opstack-settle-async-test.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks, fast iteration
2. **Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
