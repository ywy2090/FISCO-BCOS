#define BOOST_TEST_MODULE EthPipelineHookBinderTest

#include "bcos-evm/eth/reference/EthPipelineHookBinder.h"
#include "bcos-evm/eth/orchestration/DebitIntrinsicGas.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(intrinsic_policy_eip7623)
{
    EthReferenceRequest input;
    input.revisionConfig.eip7623 = true;

    EthReferenceResult output;
    EthPipelineHookBinder::HookBindingContext session{input, output};
    auto hooks = EthPipelineHookBinder::buildHooks(session);

    BOOST_CHECK_EQUAL(static_cast<int>(hooks.intrinsicPolicy.mode),
        static_cast<int>(IntrinsicDebitMode::Eip7623));
}

BOOST_AUTO_TEST_CASE(intrinsic_policy_auth_only)
{
    EthReferenceRequest input;
    input.revisionConfig.eip7623 = false;
    input.authorizationListPresent = true;
    input.authorizations.push_back({});

    EthReferenceResult output;
    EthPipelineHookBinder::HookBindingContext session{input, output};
    auto hooks = EthPipelineHookBinder::buildHooks(session);

    BOOST_CHECK_EQUAL(static_cast<int>(hooks.intrinsicPolicy.mode),
        static_cast<int>(IntrinsicDebitMode::AuthOnly));
}

BOOST_AUTO_TEST_CASE(pre_execute_precheck_early_exit)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.gas = 50'000;

    EthReferenceRequest input;
    input.message = message;
    input.gasTipCap = 3;
    input.gasFeeCap = 2;
    input.blockInfo.baseFee = 1;

    EthReferenceResult output;
    TxPipelineContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    EthPipelineHookBinder::HookBindingContext session{input, output};
    auto hooks = EthPipelineHookBinder::buildHooks(session);
    hooks.preExecute(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Malformed));
}

BOOST_AUTO_TEST_CASE(post_adopt_sets_included_tx_vmerr_flag)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 0;
    message.gas = 100'000;

    EthReferenceRequest input;
    input.message = message;

    EthReferenceResult output;
    TxPipelineContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_INVALID_INSTRUCTION;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::Unknown);

    EthPipelineHookBinder::HookBindingContext session{input, output};
    auto hooks = EthPipelineHookBinder::buildHooks(session);
    hooks.postAdopt(ctx);

    BOOST_CHECK(output.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
}

}  // namespace bcos::evm::test
