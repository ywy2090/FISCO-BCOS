#define BOOST_TEST_MODULE OpStackSettlementTest

#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip1559Access.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-evm/opstack/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/OpStackFeeSettlement.h"
#include "bcos-evm/opstack/OpStackIsthmusRevision.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(finalize_normal_completed_matches_post_execute_settlement)
{
    state::test::InMemoryStateView stateView;
    evmc_message msg{};
    msg.gas = 100'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    StateTransitionContext ctx{stateView, msg, revision, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 80'000;
    raw.gas_refund = 5'000;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
    ctx.exitKind = StateTransitionExitKind::Completed;

    OpStackFeeSidecar sidecar;
    sidecar.floorDataGas = 0;

    auto result = finalizeNormal(ctx, sidecar, ctx.exitKind);

    auto const stateRefund = gas::isEip1559GasRefundEnabled(revision) ? 5'000u : 0u;
    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, stateRefund, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(result.gasRemaining, expected.gasRemaining);
}

BOOST_AUTO_TEST_CASE(finalize_normal_intrinsic_reject_gas_used_zero)
{
    state::test::InMemoryStateView stateView;
    evmc_message msg{};
    msg.gas = 50'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    StateTransitionContext ctx{stateView, msg, revision, bcos::u256(0)};
    ctx.exitKind = StateTransitionExitKind::IntrinsicRejected;

    OpStackFeeSidecar sidecar;

    auto result = finalizeNormal(ctx, sidecar, ctx.exitKind);

    BOOST_CHECK_EQUAL(result.gasUsed, int64_t{0});
    BOOST_CHECK_EQUAL(result.gasRemaining, 50'000u);
}

BOOST_AUTO_TEST_CASE(finalize_normal_rules_rejected_applies_settlement)
{
    state::test::InMemoryStateView stateView;
    evmc_message msg{};
    msg.gas = 100'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    StateTransitionContext ctx{stateView, msg, revision, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    raw.gas_left = 60'000;
    raw.gas_refund = 0;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);
    ctx.exitKind = StateTransitionExitKind::RulesRejected;

    OpStackFeeSidecar sidecar;
    sidecar.floorDataGas = 0;

    auto result = finalizeNormal(ctx, sidecar, ctx.exitKind);

    auto const expected = postExecuteGasSettlement(100'000u, 60'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
}

BOOST_AUTO_TEST_CASE(abort_after_buy_gas_reverts_checkpoint_and_releases_gas_pool)
{
    state::test::InMemoryStateView stateView;
    evmc_address sender{};
    sender.bytes[19] = 0x42;
    stateView.insert_account(sender, state::Account{.balance = u256(1'000), .nonce = 0});

    evmc_message msg{};
    msg.gas = 50'000;
    msg.sender = sender;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    state::State state{stateView};
    StateTransitionContext ctx{state, msg, revision, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, u256(200));

    uint64_t returnRemaining = 0;
    uint64_t returnUsed = 0;
    GasPoolHooks gasPool{
        .returnGas =
            [&](uint64_t remaining, uint64_t used) {
                returnRemaining = remaining;
                returnUsed = used;
            },
    };

    OpStackExecutionResult output;
    abortNormalAfterBuyGas(ctx, gasPool, output, ctx.originalGasLimit);

    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK_EQUAL(state.get_balance(sender), u256(1'000));
    BOOST_CHECK_EQUAL(returnRemaining, 50'000u);
    BOOST_CHECK_EQUAL(returnUsed, 0u);
}

BOOST_AUTO_TEST_CASE(is_normal_pre_execution_reject_covers_intrinsic_and_gas_afford)
{
    BOOST_CHECK(isNormalPreExecutionReject(StateTransitionExitKind::IntrinsicRejected));
    BOOST_CHECK(isNormalPreExecutionReject(StateTransitionExitKind::GasAffordRejected));
    BOOST_CHECK(!isNormalPreExecutionReject(StateTransitionExitKind::Completed));
}
}  // namespace bcos::evm::test
