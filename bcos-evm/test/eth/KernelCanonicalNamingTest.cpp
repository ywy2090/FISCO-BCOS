#define BOOST_TEST_MODULE KernelCanonicalNamingTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-evm/eth/pipeline/StateTransitionExecute.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <evmc/evmc.hpp>

namespace bcos::evm::test
{
namespace
{

struct CountingPrecheckPolicy : ChainPrecheckPolicy
{
    mutable int rulesCallCount{0};
    mutable int setupCallCount{0};

    DeductIntrinsicGasParams deductIntrinsicGasParams() const override { return {}; }

    void pipelineSetupMessage(StateTransitionContext& ctx) const override
    {
        ++setupCallCount;
        (void)ctx;
    }

    void pipelineCheckRules(StateTransitionContext& ctx) const override
    {
        ++rulesCallCount;
        ctx.earlyExit = true;
    }
};

struct NoopErrorPolicy : OrchestrationErrorPolicy
{
    void onIntrinsicGasFailure(StateTransitionContext&, IntrinsicDebitFailure) const override {}
    void onPipelineException(StateTransitionContext&, std::exception_ptr) const override {}
};

}  // namespace

BOOST_AUTO_TEST_CASE(innerExecute_is_canonical_kernel_entry)
{
    BOOST_CHECK(
        (std::is_same_v<decltype(&innerExecute), InnerExecuteOutput (*)(InnerExecuteInput)>));
}

BOOST_AUTO_TEST_CASE(preCheckRules_forwards_to_pipelineCheckRules)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    CountingPrecheckPolicy policy;
    policy.preCheckRules(ctx);

    BOOST_CHECK_EQUAL(policy.rulesCallCount, 1);
    BOOST_CHECK(ctx.earlyExit);
}

BOOST_AUTO_TEST_CASE(normalizeMessage_forwards_to_pipelineSetupMessage)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    CountingPrecheckPolicy policy;
    policy.normalizeMessage(ctx);

    BOOST_CHECK_EQUAL(policy.setupCallCount, 1);
}

BOOST_AUTO_TEST_CASE(stateTransitionExecute_is_canonical_pipeline_driver)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    static evmc::VM vm{evmc_create_evmone()};
    static bcos::crypto::Keccak256 hashImpl;
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    CountingPrecheckPolicy policy;
    NoopErrorPolicy errorPolicy;
    stateTransitionExecute(ctx, policy, errorPolicy);

    BOOST_CHECK_EQUAL(policy.rulesCallCount, 1);
    BOOST_CHECK(ctx.earlyExit);
}

BOOST_AUTO_TEST_CASE(finalizeGasUsed_forwards_to_onPostExecuteNormalize)
{
    struct CountingErrorPolicy : OrchestrationErrorPolicy
    {
        mutable int normalizeCount{0};

        void onIntrinsicGasFailure(StateTransitionContext&, IntrinsicDebitFailure) const override {}
        void onPipelineException(StateTransitionContext&, std::exception_ptr) const override {}

        void onPostExecuteNormalize(StateTransitionContext& ctx) const override
        {
            ++normalizeCount;
            OrchestrationErrorPolicy::onPostExecuteNormalize(ctx);
        }
    };

    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    CountingErrorPolicy errorPolicy;
    errorPolicy.finalizeGasUsed(ctx);

    BOOST_CHECK_EQUAL(errorPolicy.normalizeCount, 1);
}

}  // namespace bcos::evm::test
