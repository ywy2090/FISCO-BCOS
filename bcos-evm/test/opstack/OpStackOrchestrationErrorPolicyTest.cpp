#define BOOST_TEST_MODULE OpStackOrchestrationErrorPolicyTest

#include "bcos-evm/opstack/OpStackOrchestrationErrorPolicy.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/orchestration/DebitIntrinsicGas.h"
#include "bcos-evm/eth/orchestration/TxPipeline.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <stdexcept>

namespace bcos::evm::test
{
namespace
{
template <typename Exception>
void invokePipelineException(
    OrchestrationErrorPolicy const& errorPolicy, TxPipelineContext& ctx, Exception exception)
{
    try
    {
        throw exception;
    }
    catch (...)
    {
        errorPolicy.onPipelineException(ctx, std::current_exception());
    }
}
}  // namespace

// O-IGF-01: intrinsic failure maps to OutOfGasLimit regardless of failure kind.
BOOST_AUTO_TEST_CASE(opstack_intrinsic_gas_failure_maps_to_out_of_gas_limit)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.gas = 21'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::OpStackIntrinsicOutOfGas);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

// O-IGF-02: failure reason is ignored (same result for GasLimitMinimum).
BOOST_AUTO_TEST_CASE(opstack_intrinsic_gas_failure_ignores_failure_kind)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.gas = 21'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::GasLimitMinimum);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

// O-PEX-01 / O-PEX-02: any pipeline exception maps to internal error.
BOOST_AUTO_TEST_CASE(opstack_pipeline_exception_maps_to_internal_error)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackOrchestrationErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, std::runtime_error{"boom"});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

BOOST_AUTO_TEST_CASE(opstack_pipeline_exception_treats_out_of_gas_like_generic)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackOrchestrationErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

// O-PEN-01: OpStack inherits base noop post-execute normalization.
BOOST_AUTO_TEST_CASE(opstack_post_execute_normalize_is_noop)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 0;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_INVALID_INSTRUCTION;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::Unknown);
    ctx.kernelOutput.logs.push_back(state::LogEntry{});

    OpStackOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(!ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INVALID_INSTRUCTION);
    BOOST_CHECK_EQUAL(ctx.kernelOutput.logs.size(), 1U);
}

// O-PCO-01: OpStack inherits base noop pipeline-complete hook.
BOOST_AUTO_TEST_CASE(opstack_pipeline_complete_is_noop)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = -5;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    OpStackOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPipelineComplete(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, -5);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
}

// INT-03: runTxPipeline routes OpStack intrinsic failures through error policy.
BOOST_AUTO_TEST_CASE(opstack_intrinsic_failure_via_run_tx_pipeline)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 1;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    TxPipelineHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::OpStackEntry;

    OpStackOrchestrationErrorPolicy errorPolicy;
    runTxPipeline(ctx, hooks, errorPolicy);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::IntrinsicRejected));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

}  // namespace bcos::evm::test
