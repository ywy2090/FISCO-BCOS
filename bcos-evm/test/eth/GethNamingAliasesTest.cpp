#define BOOST_TEST_MODULE GethNamingAliasesTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/execution/TxExecutionRunner.h"
#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/IntrinsicGasDebit.h"
#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <evmc/evmc.hpp>

namespace bcos::evm::test
{
namespace
{

struct AliasProbePolicy : ChainPrecheckPolicy
{
    mutable bool rulesCalled{false};
    mutable bool balanceCalled{false};

    IntrinsicGasDebitParams intrinsicGasDebitParams() const override { return {}; }

    void checkTransactionRules(TxPipelineContext& ctx) const override
    {
        rulesCalled = true;
        ctx.earlyExit = true;
    }

    void checkBalanceAndValue(TxPipelineContext& ctx) const override { balanceCalled = true; }
};

struct NoopErrorPolicy : OrchestrationErrorPolicy
{
    void onIntrinsicGasFailure(TxPipelineContext&, IntrinsicDebitFailure) const override {}
    void onPipelineException(TxPipelineContext&, std::exception_ptr) const override {}
};

}  // namespace

BOOST_AUTO_TEST_CASE(runTxPipeline_invokes_checkTransactionRules)
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    static evmc::VM vm{evmc_create_evmone()};
    static bcos::crypto::Keccak256 hashImpl;
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    AliasProbePolicy policy;
    NoopErrorPolicy errorPolicy;
    runTxPipeline(ctx, policy, errorPolicy);

    BOOST_CHECK(policy.rulesCalled);
    BOOST_CHECK(ctx.earlyExit);
}

BOOST_AUTO_TEST_CASE(debitIntrinsicGas_auth_only_debits_tuple_gas)
{
    IntrinsicGasDebitParams policy{};
    policy.mode = IntrinsicDebitMode::AuthOnly;
    policy.authorizationListPresent = true;
    policy.authTupleCount = 1;

    evmc_message message{};
    message.gas = 100'000;
    auto const outcome = debitIntrinsicGas(message, policy);

    BOOST_CHECK(outcome.ok);
    BOOST_CHECK(outcome.debitAmount > 0);
}

BOOST_AUTO_TEST_CASE(executeMessage_and_tx_execution_adapter_exist)
{
    BOOST_CHECK(
        (std::is_same_v<decltype(&executeMessage), ExecuteMessageOutput (*)(ExecuteMessageInput)>));
    BOOST_CHECK((std::is_same_v<decltype(&execution::TxExecutionRunner::run),
        ExecuteMessageOutput (*)(ExecuteMessageInput)>));
}

BOOST_AUTO_TEST_CASE(runExecutionFrame_is_callable)
{
    BOOST_CHECK((std::is_same_v<decltype(&execution::runExecutionFrame),
        execution::FrameResult (*)(
            execution::FrameExecutionEnv&, evmc_message, execution::FrameScope, state::EthHost&)>));
}

}  // namespace bcos::evm::test
