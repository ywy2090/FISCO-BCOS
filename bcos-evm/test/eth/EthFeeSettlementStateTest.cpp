#define BOOST_TEST_MODULE EthFeeSettlementStateTest
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
#include "bcos-evm/eth/settlement/EthSettlementProjection.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "helpers/InMemoryStateView.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addr(uint8_t b)
{
    evmc_address a{};
    a.bytes[19] = b;
    return a;
}

constexpr bcos::u256 kPreDebit = bcos::u256(21'000) * 100;
}  // namespace

BOOST_AUTO_TEST_CASE(buyGas_debits_sender_on_state)
{
    state::test::InMemoryStateView base;
    auto const initialBalance = kPreDebit + 1'000'000;
    base.insert_account(addr(1), state::Account{.balance = initialBalance});

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 21'000;
    RevisionConfig rev{};
    rev.revision = EVMC_LONDON;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(100));

    EthMessageRequest input{};
    input.blockInfo.baseFee = 10;
    input.gasPrice = 100;
    input.txValue = 0;
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(ok);
    BOOST_CHECK_EQUAL(ctx.state.get_balance(addr(1)), initialBalance - kPreDebit);
    BOOST_CHECK_EQUAL(sidecar.effectiveGasPrice, 100);
}

BOOST_AUTO_TEST_CASE(buyGas_insufficient_balance_applies_penalty)
{
    state::test::InMemoryStateView base;
    base.insert_account(addr(1), state::Account{.balance = 500});

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 21'000;
    RevisionConfig rev{};
    rev.revision = EVMC_LONDON;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(100));

    EthMessageRequest input{};
    input.blockInfo.baseFee = 10;
    input.gasPrice = 100;
    input.gasFeeCap = 100;
    input.gasTipCap = 2;
    input.hasExplicitFeeCaps = true;
    input.web3TypedTxKind = 2;
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(!ok);
    // penalty = min(500, TX_BASE_GAS * effective) — balance reduced
    BOOST_CHECK(ctx.state.get_balance(addr(1)) < 500);
    BOOST_CHECK(ctx.evmcResult.status == protocol::TransactionStatus::NotEnoughCash);
}
}  // namespace bcos::evm::test
