#define BOOST_TEST_MODULE EthOrchestrationProfileTest

#include "bcos-evm/eth/EthOrchestrationProfile.h"
#include "bcos-evm/eth/orchestration/DebitIntrinsicGas.h"
#include "bcos-evm/eth/orchestration/OrchestrationContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(intrinsic_policy_eip7623)
{
    ExecuteViaEthInput input;
    input.revisionConfig.eip7623 = true;

    ExecuteViaEthOutput output;
    EthOrchestrationProfile::Session session{input, output};
    auto hooks = EthOrchestrationProfile::buildHooks(session);

    BOOST_CHECK_EQUAL(static_cast<int>(hooks.intrinsicPolicy.mode),
        static_cast<int>(IntrinsicDebitMode::Eip7623));
}

BOOST_AUTO_TEST_CASE(intrinsic_policy_auth_only)
{
    ExecuteViaEthInput input;
    input.revisionConfig.eip7623 = false;
    input.authorizationListPresent = true;
    input.authorizations.push_back({});

    ExecuteViaEthOutput output;
    EthOrchestrationProfile::Session session{input, output};
    auto hooks = EthOrchestrationProfile::buildHooks(session);

    BOOST_CHECK_EQUAL(static_cast<int>(hooks.intrinsicPolicy.mode),
        static_cast<int>(IntrinsicDebitMode::AuthOnly));
}

BOOST_AUTO_TEST_CASE(pre_execute_precheck_early_exit)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 50'000;

    ExecuteViaEthInput input;
    input.message = message;
    input.gasTipCap = 3;
    input.gasFeeCap = 2;
    input.blockInfo.baseFee = 1;

    ExecuteViaEthOutput output;
    OrchestrationContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    EthOrchestrationProfile::Session session{input, output};
    auto hooks = EthOrchestrationProfile::buildHooks(session);
    hooks.preExecute(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Malformed));
}

BOOST_AUTO_TEST_CASE(post_adopt_sets_included_tx_vmerr_flag)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.depth = 0;
    message.gas = 100'000;

    ExecuteViaEthInput input;
    input.message = message;

    ExecuteViaEthOutput output;
    OrchestrationContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_INVALID_INSTRUCTION;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::Unknown);

    EthOrchestrationProfile::Session session{input, output};
    auto hooks = EthOrchestrationProfile::buildHooks(session);
    hooks.postAdopt(ctx);

    BOOST_CHECK(output.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
}

}  // namespace bcos::evm::test
