#define BOOST_TEST_MODULE GethNamingAliasesTest

#include "bcos-evm/eth/GethNamingAliases.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/IntrinsicGasDebit.h"
#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-evm/eth/pipeline/TxPipeline.h"
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

    IntrinsicGasDebitParams intrinsicGasDebitParams() const override { return {}; }

    void pipelineSetupMessage(TxPipelineContext& ctx) const override
    {
        ++setupCallCount;
        (void)ctx;
    }

    void pipelineCheckRules(TxPipelineContext& ctx) const override
    {
        ++rulesCallCount;
        ctx.earlyExit = true;
    }
};

struct NoopErrorPolicy : OrchestrationErrorPolicy
{
    void onIntrinsicGasFailure(TxPipelineContext&, IntrinsicDebitFailure) const override {}
    void onPipelineException(TxPipelineContext&, std::exception_ptr) const override {}
};

}  // namespace

BOOST_AUTO_TEST_CASE(debitIntrinsicGas_deprecated_alias_matches_deductIntrinsicGas_none_mode)
{
    IntrinsicGasDebitParams policy{};
    policy.mode = IntrinsicDebitMode::None;

    evmc_message debitMsg{};
    evmc_message deductMsg{};
    debitMsg.gas = 50'000;
    deductMsg.gas = 50'000;

    auto const debitOutcome = debitIntrinsicGas(debitMsg, policy);
    auto const deductOutcome = deductIntrinsicGas(deductMsg, policy);

    BOOST_CHECK(debitOutcome.ok);
    BOOST_CHECK(deductOutcome.ok);
    BOOST_CHECK_EQUAL(debitOutcome.debitAmount, deductOutcome.debitAmount);
    BOOST_CHECK_EQUAL(debitMsg.gas, deductMsg.gas);
}

BOOST_AUTO_TEST_CASE(innerExecute_resolves_as_executeMessage_forward)
{
    BOOST_CHECK(
        (std::is_same_v<decltype(&innerExecute), ExecuteMessageOutput (*)(ExecuteMessageInput)>));
    BOOST_CHECK((std::is_same_v<decltype(&innerExecute), decltype(&executeMessage)>));
}

BOOST_AUTO_TEST_CASE(preCheckRules_forwards_to_pipelineCheckRules)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    CountingPrecheckPolicy policy;
    policy.preCheckRules(ctx);

    BOOST_CHECK_EQUAL(policy.rulesCallCount, 1);
    BOOST_CHECK(ctx.earlyExit);
}

BOOST_AUTO_TEST_CASE(normalizeMessage_forwards_to_pipelineSetupMessage)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    CountingPrecheckPolicy policy;
    policy.normalizeMessage(ctx);

    BOOST_CHECK_EQUAL(policy.setupCallCount, 1);
}

BOOST_AUTO_TEST_CASE(stateTransitionExecute_forwards_to_runTxPipeline)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
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

        void onIntrinsicGasFailure(TxPipelineContext&, IntrinsicDebitFailure) const override {}
        void onPipelineException(TxPipelineContext&, std::exception_ptr) const override {}

        void onPostExecuteNormalize(TxPipelineContext& ctx) const override
        {
            ++normalizeCount;
            OrchestrationErrorPolicy::onPostExecuteNormalize(ctx);
        }
    };

    state::test::InMemoryStateView stateView;
    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    CountingErrorPolicy errorPolicy;
    errorPolicy.finalizeGasUsed(ctx);

    BOOST_CHECK_EQUAL(errorPolicy.normalizeCount, 1);
}

BOOST_AUTO_TEST_CASE(evm_frame_aliases_share_runCallFrame_signature)
{
    using FrameFn = execution::FrameResult (*)(
        execution::FrameExecutionEnv&, evmc_message, execution::FrameScope, state::EthHost&);
    BOOST_CHECK((std::is_same_v<decltype(&execution::evmCall), FrameFn>));
    BOOST_CHECK((std::is_same_v<decltype(&execution::evmCreate), FrameFn>));
    BOOST_CHECK((std::is_same_v<decltype(&execution::evmDelegateCall), FrameFn>));
    BOOST_CHECK((std::is_same_v<decltype(&execution::evmStaticCall), FrameFn>));
}

}  // namespace bcos::evm::test
