#define BOOST_TEST_MODULE EthStateTransitionBindingsTest

#include "bcos-evm/eth/apply/EthStateTransitionBindings.h"
#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(intrinsic_policy_eip7623)
{
    EthMessageRequest input;
    input.revisionConfig.eip7623 = true;

    EthMessageResult output;
    EthStateTransitionBindings::Context ctx{input, output};
    auto policy = EthStateTransitionBindings::buildStateTransitionHooks(ctx);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::Eip7623));
}

BOOST_AUTO_TEST_CASE(intrinsic_policy_auth_only)
{
    EthMessageRequest input;
    input.revisionConfig.eip7623 = false;
    input.authorizationListPresent = true;
    input.authorizations.push_back({});

    EthMessageResult output;
    EthStateTransitionBindings::Context ctx{input, output};
    auto policy = EthStateTransitionBindings::buildStateTransitionHooks(ctx);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::AuthOnly));
}

BOOST_AUTO_TEST_CASE(pre_execute_precheck_early_exit)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 50'000;

    EthMessageRequest input;
    input.message = message;
    input.revisionConfig.eip1559 = true;
    input.gasTipCap = 3;
    input.gasFeeCap = 2;
    input.blockInfo.baseFee = 1;

    EthMessageResult output;
    StateTransitionContext stCtx{stateView, message, input.revisionConfig, bcos::u256(0)};

    EthStateTransitionBindings::Context ctx{input, output};
    auto policy = EthStateTransitionBindings::buildStateTransitionHooks(ctx);
    policy.onPreCheckRules(stCtx);

    BOOST_CHECK(stCtx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(stCtx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Malformed));
}

BOOST_AUTO_TEST_CASE(bind_returns_precheck_policy_and_error_policy)
{
    EthMessageRequest input;
    EthMessageResult output;
    EthStateTransitionBindings::Context ctx{input, output};
    auto bindings = EthStateTransitionBindings::bind(ctx);

    BOOST_CHECK_EQUAL(static_cast<int>(bindings.hooks.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::None));
}

}  // namespace bcos::evm::test
