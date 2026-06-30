#define BOOST_TEST_MODULE FiscoOrchestrationErrorPolicyTest

#include "bcos-evm/bcos/FiscoOrchestrationErrorPolicy.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoPipelineInternals.h"
#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/IntrinsicGasDebit.h"
#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>
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

FiscoOrchestrationErrorPolicy makeFiscoErrorPolicy(bool fixErrorHandling = false)
{
    static crypto::Keccak256 hashImpl;
    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.hashImpl = &hashImpl;
    errorPolicy.fixErrorHandling = fixErrorHandling;
    return errorPolicy;
}
}  // namespace

// F-IGF-01: GasLimitMinimum preserves message gas when fixErrorHandling is off.
BOOST_AUTO_TEST_CASE(fisco_intrinsic_gas_failure_gas_limit_minimum)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 5'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::GasLimitMinimum);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 5'000);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGas));
}

// F-IGF-04: fixErrorHandling clamps gas_left to zero on intrinsic failure.
BOOST_AUTO_TEST_CASE(fisco_intrinsic_gas_failure_clamps_gas_when_fix_error_handling)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 5'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(true);
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::CalldataOutOfGas);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
}

// F-IGF-02: CalldataOutOfGas carries calldata-specific reason in output.
BOOST_AUTO_TEST_CASE(fisco_intrinsic_gas_failure_calldata_out_of_gas)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 5'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::CalldataOutOfGas);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 5'000);
    BOOST_CHECK_GT(ctx.evmcResult.output_size, 0U);
}

// F-IGF-03: AuthTupleOutOfGas carries auth-tuple reason in output.
BOOST_AUTO_TEST_CASE(fisco_intrinsic_gas_failure_auth_tuple_out_of_gas)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 5'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::AuthTupleOutOfGas);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 5'000);
    BOOST_CHECK_GT(ctx.evmcResult.output_size, 0U);
}

// F-PEX-01: OutOfGas exception maps with zero gas left.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_maps_out_of_gas)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 40'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGas));
}

// F-PEX-02: NotEnoughCash preserves message gas when fixErrorHandling is off.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_maps_not_enough_cash)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 55'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    invokePipelineException(errorPolicy, ctx, protocol::NotEnoughCashError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 55'000);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::NotEnoughCash));
}

// F-PEX-05: NotFoundCode on DELEGATECALL succeeds and keeps gas.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_not_found_code_on_delegatecall_succeeds)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.gas = 50'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    invokePipelineException(errorPolicy, ctx, NotFoundCodeError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 50'000);
}

// F-PEX-07: fixErrorHandling maps generic exceptions to Unknown with zero gas.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_generic_maps_to_unknown_with_fix)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 60'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(true);
    invokePipelineException(errorPolicy, ctx, protocol::PrecompiledError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

// F-PEX-08: fixErrorHandling clamps NotEnoughCash gas_left to zero.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_not_enough_cash_clamps_gas_with_fix)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 55'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(true);
    invokePipelineException(errorPolicy, ctx, protocol::NotEnoughCashError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
}

// F-PEX-09: pipeline exception reverts an open checkpoint.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_reverts_open_checkpoint)
{
    state::test::InMemoryStateView stateView;

    evmc_address sender{};
    sender.bytes[19] = 0x03;
    state::Account senderAccount;
    senderAccount.balance = 2'000;
    stateView.insert_account(sender, senderAccount);

    evmc_message message{};
    message.sender = sender;
    message.gas = 100'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, 200);
    BOOST_CHECK(ctx.state.has_checkpoint());

    auto errorPolicy = makeFiscoErrorPolicy(false);
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), 2'000);
}

// F-PCO-01: negative gas_left preserves message gas when fixErrorHandling is off.
BOOST_AUTO_TEST_CASE(fisco_pipeline_complete_preserves_message_gas_without_fix)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 100'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = -5;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    auto errorPolicy = makeFiscoErrorPolicy(false);
    errorPolicy.onPipelineComplete(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 100'000);
}

// F-PCO-03: normal gas_left is unchanged.
BOOST_AUTO_TEST_CASE(fisco_pipeline_complete_leaves_positive_gas_left)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 100'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 88'000;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    auto errorPolicy = makeFiscoErrorPolicy(false);
    errorPolicy.onPipelineComplete(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 88'000);
}

// F-PEX-03: NotFoundCode on a plain CALL reverts with call-address error.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_not_found_code_on_call_reverts)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    invokePipelineException(errorPolicy, ctx, NotFoundCodeError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::RevertInstruction));
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 50'000);
}

// F-PEX-04: NotFoundCode on STATICCALL succeeds and keeps gas.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_not_found_code_on_staticcall_succeeds)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.flags = EVMC_STATIC;
    message.gas = 50'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    invokePipelineException(errorPolicy, ctx, NotFoundCodeError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 50'000);
}

// F-PEX-06: legacy generic exception maps to OutOfGas when fixErrorHandling is off.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_generic_maps_to_out_of_gas_legacy)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 60'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    auto errorPolicy = makeFiscoErrorPolicy(false);
    invokePipelineException(errorPolicy, ctx, protocol::PrecompiledError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 60'000);
}

// F-PCO-02: negative gas_left is clamped when fixErrorHandling is on.
BOOST_AUTO_TEST_CASE(fisco_pipeline_complete_clamps_negative_gas_left)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 100'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = -5;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    auto errorPolicy = makeFiscoErrorPolicy(true);
    errorPolicy.onPipelineComplete(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGas));
}

BOOST_AUTO_TEST_CASE(fisco_post_execute_patches_empty_create_address)
{
    state::test::InMemoryStateView stateView;

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

// F-PEN-02: CREATE2 empty create_address is patched from recipient.
BOOST_AUTO_TEST_CASE(fisco_post_execute_patches_empty_create2_address)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_CREATE2;
    message.recipient.bytes[19] = 0xCD;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(std::memcmp(ctx.evmcResult.create_address.bytes, message.recipient.bytes,
                    sizeof(message.recipient.bytes)) == 0);
}

// F-PEN-03: non-empty create_address is not overwritten.
BOOST_AUTO_TEST_CASE(fisco_post_execute_keeps_nonempty_create_address)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.recipient.bytes[19] = 0xAB;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
    ctx.evmcResult.create_address.bytes[19] = 0x42;

    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.create_address.bytes[19], 0x42);
}

// F-PEN-06: fixRevertLogs does not clear logs on SUCCESS.
BOOST_AUTO_TEST_CASE(fisco_post_execute_keeps_logs_on_success_with_fix_revert_logs)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
    ctx.kernelOutput.logs.push_back(state::LogEntry{});

    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.fixRevertLogs = true;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK_EQUAL(ctx.kernelOutput.logs.size(), 1U);
}

BOOST_AUTO_TEST_CASE(fisco_post_execute_clears_logs_on_revert_when_fix_revert_logs)
{
    state::test::InMemoryStateView stateView;

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
    state::test::InMemoryStateView stateView;

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

// INT-02: stateTransitionExecute routes balance exceptions through Fisco error policy.
BOOST_AUTO_TEST_CASE(fisco_pipeline_exception_via_run_tx_pipeline)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    struct ThrowBalancePrecheckPolicy : ChainPrecheckPolicy
    {
        IntrinsicGasDebitParams intrinsicGasDebitParams() const override
        {
            IntrinsicGasDebitParams policy;
            policy.mode = IntrinsicDebitMode::None;
            return policy;
        }

        void pipelineCheckBalance(TxPipelineContext&) const override
        {
            throw protocol::NotEnoughCashError{};
        }
    };

    ThrowBalancePrecheckPolicy precheckPolicy;

    auto errorPolicy = makeFiscoErrorPolicy(false);
    stateTransitionExecute(ctx, precheckPolicy, errorPolicy);

    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::ExceptionHandled));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 100'000);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::NotEnoughCash));
}

}  // namespace bcos::evm::test
