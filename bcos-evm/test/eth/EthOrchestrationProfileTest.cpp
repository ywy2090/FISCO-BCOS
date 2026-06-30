#define BOOST_TEST_MODULE EthOrchestrationProfileTest

#include "bcos-evm/eth/apply/EthOrchestrationProfile.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(intrinsic_policy_eip7623)
{
    EthReferenceRequest input;
    input.revisionConfig.eip7623 = true;

    EthReferenceResult output;
    EthOrchestrationProfile::BindingsContext bindingsCtx{input, output};
    auto policy = EthOrchestrationProfile::buildPrecheckPolicy(bindingsCtx);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::Eip7623));
}

BOOST_AUTO_TEST_CASE(intrinsic_policy_auth_only)
{
    EthReferenceRequest input;
    input.revisionConfig.eip7623 = false;
    input.authorizationListPresent = true;
    input.authorizations.push_back({});

    EthReferenceResult output;
    EthOrchestrationProfile::BindingsContext bindingsCtx{input, output};
    auto policy = EthOrchestrationProfile::buildPrecheckPolicy(bindingsCtx);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::AuthOnly));
}

BOOST_AUTO_TEST_CASE(pre_execute_precheck_early_exit)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 50'000;

    EthReferenceRequest input;
    input.message = message;
    input.revisionConfig.eip1559 = true;
    input.gasTipCap = 3;
    input.gasFeeCap = 2;
    input.blockInfo.baseFee = 1;

    EthReferenceResult output;
    StateTransitionContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    EthOrchestrationProfile::BindingsContext bindingsCtx{input, output};
    auto policy = EthOrchestrationProfile::buildPrecheckPolicy(bindingsCtx);
    policy.onPreCheckRules(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Malformed));
}

BOOST_AUTO_TEST_CASE(bind_returns_precheck_policy_and_error_policy)
{
    EthReferenceRequest input;
    EthReferenceResult output;
    EthOrchestrationProfile::BindingsContext bindingsCtx{input, output};
    auto bindings = EthOrchestrationProfile::bind(bindingsCtx);

    BOOST_CHECK_EQUAL(static_cast<int>(bindings.precheckPolicy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::None));
}

}  // namespace bcos::evm::test
