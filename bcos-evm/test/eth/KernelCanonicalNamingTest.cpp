#define BOOST_TEST_MODULE KernelCanonicalNamingTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/core/StateTransitionHooks.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionErrorPolicy.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionExecute.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <evmc/evmc.hpp>

namespace bcos::evm::test
{
namespace
{

struct CountingStateTransitionHooks : StateTransitionHooks
{
    mutable int rulesCallCount{0};
    mutable int setupCallCount{0};

    DeductIntrinsicGasParams getIntrinsicGasParams() const override { return {}; }

    void onNormalizeMessage(StateTransitionContext& ctx) const override
    {
        ++setupCallCount;
        (void)ctx;
    }

    void onPreCheckRules(StateTransitionContext& ctx) const override
    {
        ++rulesCallCount;
        ctx.earlyExit = true;
    }
};

struct NoopErrorPolicy : StateTransitionErrorPolicy
{
    void onIntrinsicGasFailure(StateTransitionContext&, IntrinsicDebitFailure) const override {}
    void onException(StateTransitionContext&, std::exception_ptr) const override {}
};

}  // namespace

BOOST_AUTO_TEST_CASE(innerExecute_is_canonical_kernel_entry)
{
    BOOST_CHECK(
        (std::is_same_v<decltype(&innerExecute), InnerExecuteOutput (*)(InnerExecuteInput)>));
}

BOOST_AUTO_TEST_CASE(onPreCheckRules_is_hook_override_point)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{stateView, message, bcos::evm::RevisionConfig{}, bcos::u256(0)};

    CountingStateTransitionHooks policy;
    policy.onPreCheckRules(ctx);

    BOOST_CHECK_EQUAL(policy.rulesCallCount, 1);
    BOOST_CHECK(ctx.earlyExit);
}

BOOST_AUTO_TEST_CASE(onNormalizeMessage_is_hook_override_point)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{stateView, message, bcos::evm::RevisionConfig{}, bcos::u256(0)};

    CountingStateTransitionHooks policy;
    policy.onNormalizeMessage(ctx);

    BOOST_CHECK_EQUAL(policy.setupCallCount, 1);
}

BOOST_AUTO_TEST_CASE(stateTransitionExecute_is_canonical_pipeline_driver)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{stateView, message, bcos::evm::RevisionConfig{}, bcos::u256(0)};
    static evmc::VM vm{evmc_create_evmone()};
    static bcos::crypto::Keccak256 hashImpl;
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    CountingStateTransitionHooks policy;
    NoopErrorPolicy errorPolicy;
    stateTransitionExecute(ctx, policy, errorPolicy);

    BOOST_CHECK_EQUAL(policy.rulesCallCount, 1);
    BOOST_CHECK(ctx.earlyExit);
}

BOOST_AUTO_TEST_CASE(onFinalizeGasUsed_is_error_policy_hook)
{
    struct CountingErrorPolicy : StateTransitionErrorPolicy
    {
        mutable int normalizeCount{0};

        void onIntrinsicGasFailure(StateTransitionContext&, IntrinsicDebitFailure) const override {}
        void onException(StateTransitionContext&, std::exception_ptr) const override {}

        void onFinalizeGasUsed(StateTransitionContext& ctx) const override
        {
            ++normalizeCount;
            StateTransitionErrorPolicy::onFinalizeGasUsed(ctx);
        }
    };

    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{stateView, message, bcos::evm::RevisionConfig{}, bcos::u256(0)};

    CountingErrorPolicy errorPolicy;
    errorPolicy.onFinalizeGasUsed(ctx);

    BOOST_CHECK_EQUAL(errorPolicy.normalizeCount, 1);
}

}  // namespace bcos::evm::test
