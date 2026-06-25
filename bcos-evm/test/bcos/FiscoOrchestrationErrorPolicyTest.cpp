#define BOOST_TEST_MODULE FiscoOrchestrationErrorPolicyTest

#include "bcos-evm/bcos/FiscoOrchestrationErrorPolicy.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "state/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(fisco_post_execute_patches_empty_create_address)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.recipient.bytes[19] = 0xAB;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(std::memcmp(ctx.evmcResult.create_address.bytes, message.recipient.bytes,
                    sizeof(message.recipient.bytes)) == 0);
}

BOOST_AUTO_TEST_CASE(fisco_post_execute_clears_logs_on_revert_when_fix_revert_logs)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);
    ctx.kernelOutput.logs.push_back(state::LogEntry{});

    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.fixRevertLogs = true;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(ctx.kernelOutput.logs.empty());
}

BOOST_AUTO_TEST_CASE(fisco_post_execute_keeps_logs_on_revert_without_fix_revert_logs)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);
    ctx.kernelOutput.logs.push_back(state::LogEntry{});

    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.fixRevertLogs = false;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK_EQUAL(ctx.kernelOutput.logs.size(), 1U);
}

}  // namespace bcos::evm::test
