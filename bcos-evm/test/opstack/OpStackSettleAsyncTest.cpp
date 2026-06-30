#define BOOST_TEST_MODULE OpStackSettleAsyncTest

#include "opstack/helpers/OpStackSettleTestHelpers.h"
#include <bcos-framework/executor/OpStackTxType.h>
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(settle_normal_completed_wires_refund_and_gas_pool)
{
    NormalSettleFixture fixture(100'000, StateTransitionExitKind::Completed, 80'000);
    fixture.prepareAndComplete();
    auto const senderAfterBuyGas = fixture.ctx.state.get_balance(fixture.sender);

    auto const output = fixture.completeAfterPipeline();

    assertCompleteOutputMatchesFinalizeOracle(fixture, output);
    BOOST_CHECK_GT(fixture.ctx.state.get_balance(fixture.sender), senderAfterBuyGas);
}

BOOST_AUTO_TEST_CASE(settle_normal_rules_rejected_wires_partial_refund)
{
    NormalSettleFixture fixture(
        100'000, StateTransitionExitKind::RulesRejected, 60'000, EVMC_REVERT);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_GT(output.gasUsed, int64_t{0});
    assertCompleteOutputMatchesFinalizeOracle(fixture, output);
}

BOOST_AUTO_TEST_CASE(settle_normal_exception_handled_wires_partial_refund)
{
    NormalSettleFixture fixture(
        100'000, StateTransitionExitKind::ExceptionHandled, 60'000, EVMC_REVERT);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_GT(output.gasUsed, int64_t{0});
    assertCompleteOutputMatchesFinalizeOracle(fixture, output);
}

BOOST_AUTO_TEST_CASE(settle_normal_intrinsic_reject_return_gas_full_limit)
{
    NormalSettleFixture fixture(50'000, StateTransitionExitKind::IntrinsicRejected, 0);
    auto const balanceBeforeBuyGas = fixture.ctx.state.get_balance(fixture.sender);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), balanceBeforeBuyGas);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastRemaining, 50'000u);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 0u);
}

BOOST_AUTO_TEST_CASE(settle_normal_gas_afford_reject_return_gas_full_limit)
{
    NormalSettleFixture fixture(50'000, StateTransitionExitKind::GasAffordRejected, 0);
    auto const balanceBeforeBuyGas = fixture.ctx.state.get_balance(fixture.sender);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), balanceBeforeBuyGas);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastRemaining, 50'000u);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 0u);
}

BOOST_AUTO_TEST_CASE(settle_normal_null_return_gas_hook_no_crash)
{
    NormalSettleFixture fixture(100'000, StateTransitionExitKind::Completed, 80'000);
    fixture.checkpointBeforeBuyGas();
    BOOST_REQUIRE(fixture.buyGas());

    GasPoolHooks emptyHooks{};
    OpStackExecutionResult output;
    task::syncWait(fixture.settlement.completeAfterPipeline(
        fixture.view, fixture.feeParams, emptyHooks, output));

    auto const oracle = finalizeNormal(fixture.ctx, fixture.sidecar, fixture.ctx.exitKind);
    BOOST_CHECK_EQUAL(output.gasUsed, oracle.gasUsed);
}

BOOST_AUTO_TEST_CASE(settle_normal_call_frame_skips_refund_routing)
{
    NormalSettleFixture fixture(100'000, StateTransitionExitKind::Completed, 80'000);
    fixture.input.call = true;
    fixture.input.skipTransactionChecks = true;
    fixture.input.noBaseFee = true;
    fixture.input.gasTipCap = 0;
    fixture.input.gasFeeCap = 0;
    fixture.prepareAndComplete();

    auto const senderAfterBuyGas = fixture.ctx.state.get_balance(fixture.sender);
    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), senderAfterBuyGas);
    assertGasPoolMatchesSettled(
        fixture.spy, finalizeNormal(fixture.ctx, fixture.sidecar, fixture.ctx.exitKind));
    BOOST_CHECK_EQUAL(
        output.gasUsed, finalizeNormal(fixture.ctx, fixture.sidecar, fixture.ctx.exitKind).gasUsed);
}

BOOST_AUTO_TEST_CASE(settle_normal_deposit_flag_skips_refund_routing)
{
    NormalSettleFixture fixture(100'000, StateTransitionExitKind::Completed, 80'000);
    fixture.input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    fixture.prepareAndComplete();

    auto const senderAfterBuyGas = fixture.ctx.state.get_balance(fixture.sender);
    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), senderAfterBuyGas);
    assertGasPoolMatchesSettled(
        fixture.spy, finalizeNormal(fixture.ctx, fixture.sidecar, fixture.ctx.exitKind));
    BOOST_CHECK_EQUAL(
        output.gasUsed, finalizeNormal(fixture.ctx, fixture.sidecar, fixture.ctx.exitKind).gasUsed);
}

BOOST_AUTO_TEST_CASE(settle_deposit_success_commits_and_returns_gas_pool)
{
    DepositSettleFixture fixture(100'000, StateTransitionExitKind::Completed, EVMC_SUCCESS, 80'000);

    auto const settled = task::syncWait(
        settleDeposit(fixture.ctx, fixture.ctx.exitKind, EVMC_SUCCESS, fixture.spy.hooks()));

    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(settled.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(settled.gasRemaining, expected.gasRemaining);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), bcos::u256(999));
    assertGasPoolMatchesSettled(fixture.spy, settled);
}

BOOST_AUTO_TEST_CASE(settle_deposit_revert_returns_actual_gas)
{
    DepositSettleFixture fixture(50'000, StateTransitionExitKind::Completed, EVMC_REVERT, 29'000);

    auto const settled = task::syncWait(
        settleDeposit(fixture.ctx, fixture.ctx.exitKind, EVMC_REVERT, fixture.spy.hooks()));

    BOOST_CHECK_LT(settled.gasUsed, 50'000);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), bcos::u256(0));
    assertGasPoolMatchesSettled(fixture.spy, settled);
}

BOOST_AUTO_TEST_CASE(settle_deposit_entry_failure_uses_gas_limit)
{
    DepositSettleFixture fixture(
        20'999, StateTransitionExitKind::IntrinsicRejected, EVMC_OUT_OF_GAS, 0);

    auto const settled = task::syncWait(settleDeposit(fixture.ctx,
        StateTransitionExitKind::IntrinsicRejected, EVMC_OUT_OF_GAS, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(settled.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), bcos::u256(0));
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 20'999u);
}

BOOST_AUTO_TEST_CASE(settle_deposit_gas_afford_reject_entry_failure)
{
    DepositSettleFixture fixture(
        20'999, StateTransitionExitKind::GasAffordRejected, EVMC_OUT_OF_GAS, 0);

    auto const settled = task::syncWait(settleDeposit(fixture.ctx,
        StateTransitionExitKind::GasAffordRejected, EVMC_OUT_OF_GAS, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(settled.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 20'999u);
}

BOOST_AUTO_TEST_CASE(settle_deposit_null_return_gas_hook_no_crash)
{
    DepositSettleFixture fixture(100'000, StateTransitionExitKind::Completed, EVMC_SUCCESS, 80'000);

    GasPoolHooks emptyHooks{};
    auto const settled =
        task::syncWait(settleDeposit(fixture.ctx, fixture.ctx.exitKind, EVMC_SUCCESS, emptyHooks));

    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_CHECK(!fixture.ctx.state.has_checkpoint());
    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(settled.gasUsed, static_cast<int64_t>(expected.gasUsed));
}

BOOST_AUTO_TEST_CASE(settle_deposit_exception_handled_entry_failure)
{
    DepositSettleFixture fixture(
        20'999, StateTransitionExitKind::ExceptionHandled, EVMC_OUT_OF_GAS, 0);

    auto const settled = task::syncWait(settleDeposit(fixture.ctx,
        StateTransitionExitKind::ExceptionHandled, EVMC_OUT_OF_GAS, fixture.spy.hooks()));

    BOOST_CHECK_EQUAL(settled.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(settled.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_nonce(fixture.sender), 6u);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 20'999u);
}
}  // namespace bcos::evm::test
