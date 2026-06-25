#define BOOST_TEST_MODULE OrchestrationErrorPolicyTest

#include "bcos-evm/eth/orchestration/EthOrchestrationErrorPolicy.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(eth_post_execute_normalizes_included_top_level_vmerr)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 0;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_INVALID_INSTRUCTION;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::Unknown);

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
}

BOOST_AUTO_TEST_CASE(eth_post_execute_skips_nested_vmerr_normalization)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 1;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_INVALID_INSTRUCTION;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::Unknown);

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(!ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INVALID_INSTRUCTION);
}

BOOST_AUTO_TEST_CASE(eth_post_execute_normalizes_set_code_revert_at_top_level)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 0;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    ctx.inputs.authorizationListPresent = true;

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
}

}  // namespace bcos::evm::test
