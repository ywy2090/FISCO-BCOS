#define BOOST_TEST_MODULE OpStackDepositSettlementTest

#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/opstack/OpStackIsthmusRevision.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
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
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x61);
    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = 5});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 100'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    StateTransitionContext ctx{stateView, msg, revision, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, bcos::u256(999));

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 80'000;
    raw.gas_refund = 0;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    auto const result = finalizeDeposit(ctx, StateTransitionExitKind::Completed, EVMC_SUCCESS);

    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(result.gasRemaining, expected.gasRemaining);
    BOOST_CHECK_EQUAL(ctx.state.get_nonce(sender), 6u);
    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), bcos::u256(999));
}

BOOST_AUTO_TEST_CASE(deposit_revert_actual_gas_reverts_state_but_bumps_nonce)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x62);
    stateView.insert_account(sender, state::Account{.balance = 50, .nonce = 7});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 50'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    StateTransitionContext ctx{stateView, msg, revision, bcos::u256(0)};

    auto const balanceBefore = ctx.state.get_balance(sender);
    ctx.state.checkpoint();
    ctx.state.set_balance(sender, balanceBefore + bcos::u256(100));

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    raw.gas_left = 29'000;
    raw.gas_refund = 0;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);

    auto const result = finalizeDeposit(ctx, StateTransitionExitKind::Completed, EVMC_REVERT);

    auto const expected = postExecuteGasSettlement(50'000u, 29'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_LT(result.gasUsed, 50'000);
    BOOST_CHECK_EQUAL(ctx.state.get_nonce(sender), 8u);
    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), balanceBefore);
}

BOOST_AUTO_TEST_CASE(deposit_entry_failure_uses_gas_limit_and_bumps_nonce)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x63);
    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = 5});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 20'999;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    StateTransitionContext ctx{stateView, msg, revision, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, bcos::u256(123));

    auto const result =
        finalizeDeposit(ctx, StateTransitionExitKind::IntrinsicRejected, EVMC_OUT_OF_GAS);

    BOOST_CHECK_EQUAL(result.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(result.gasRemaining, 0u);
    BOOST_CHECK_EQUAL(ctx.state.get_nonce(sender), 6u);
    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), bcos::u256(0));
}
}  // namespace bcos::evm::test
