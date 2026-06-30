#define BOOST_TEST_MODULE OpStackNormalTxFeeCoordinatorTest

// decision tree via OpStackNormalTxFeeCoordinator deep module (PR2).

#include "opstack/helpers/OpStackSettleTestHelpers.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
void zeroSenderBalance(NormalSettleFixture& fixture)
{
    fixture.stateView.insert_account(
        fixture.sender, state::Account{.balance = u256(0), .nonce = 0});
}
}  // namespace

BOOST_AUTO_TEST_CASE(buy_gas_failure_aborts_without_commit)
{
    NormalSettleFixture fixture(100'000, StateTransitionExitKind::Completed, 80'000);
    zeroSenderBalance(fixture);

    fixture.checkpointBeforeBuyGas();
    OpStackExecutionResult output;
    auto const ok =
        task::syncWait(fixture.settlement.buyGas(fixture.view, fixture.spy.hooks(), output));

    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastRemaining, 100'000u);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 0u);
}

BOOST_AUTO_TEST_CASE(complete_after_pipeline_intrinsic_reject_aborts_adr025)
{
    NormalSettleFixture fixture(50'000, StateTransitionExitKind::IntrinsicRejected, 0);
    auto const initialBalance = fixture.ctx.state.get_balance(fixture.sender);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), initialBalance);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastRemaining, 50'000u);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 0u);
}

BOOST_AUTO_TEST_CASE(complete_after_pipeline_gas_afford_reject_aborts_adr025)
{
    NormalSettleFixture fixture(50'000, StateTransitionExitKind::GasAffordRejected, 0);
    auto const initialBalance = fixture.ctx.state.get_balance(fixture.sender);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(fixture.ctx.state.get_balance(fixture.sender), initialBalance);
    BOOST_REQUIRE_EQUAL(fixture.spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(fixture.spy.lastRemaining, 50'000u);
    BOOST_CHECK_EQUAL(fixture.spy.lastUsed, 0u);
}

BOOST_AUTO_TEST_CASE(complete_after_pipeline_completed_projects_receipt_meta)
{
    NormalSettleFixture fixture(100'000, StateTransitionExitKind::Completed, 80'000);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_GT(output.gasUsed, int64_t{0});
    BOOST_REQUIRE(output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.l1Fee, fixture.sidecar.l1CostCharged);
    assertCompleteOutputMatchesFinalizeOracle(fixture, output);
}

BOOST_AUTO_TEST_CASE(complete_after_pipeline_rules_rejected_still_settles)
{
    NormalSettleFixture fixture(
        100'000, StateTransitionExitKind::RulesRejected, 60'000, EVMC_REVERT);
    fixture.prepareAndComplete();

    auto const output = fixture.completeAfterPipeline();

    BOOST_CHECK_GT(output.gasUsed, int64_t{0});
    assertCompleteOutputMatchesFinalizeOracle(fixture, output);
}
}  // namespace bcos::evm::test
