#define BOOST_TEST_MODULE OrchestrationErrorPolicyTest

#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-evm/eth/pipeline/DebitIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/eth/reference/EthOrchestrationErrorPolicy.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryEvmStateReader.h"
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

// E-IGF-01: intrinsic failure maps to OutOfGasLimit with zero gas left.
BOOST_AUTO_TEST_CASE(eth_intrinsic_gas_failure_maps_to_out_of_gas_limit)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.gas = 5'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::GasLimitMinimum);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

// E-IGF-02: intrinsic failure reason does not change the mapped result.
BOOST_AUTO_TEST_CASE(eth_intrinsic_gas_failure_ignores_failure_kind)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.gas = 5'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::CalldataOutOfGas);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

// E-PEX-02: generic BCOS exception maps to internal error.
BOOST_AUTO_TEST_CASE(eth_pipeline_exception_maps_generic_exception)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    EthOrchestrationErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, protocol::PrecompiledError{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

// GAP-003 E-PEX-05: non-OutOfGas BCOS exception maps to INTERNAL_ERROR + Unknown without rethrow.
// GETH_ORACLE: go-ethereum/core/state_transition.go:550-552 preCheck err -> block reject (no EVMC
// layer). CURRENT_ORACLE: EVMC_INTERNAL_ERROR + TransactionStatus::Unknown; handler must not
// propagate throw.
BOOST_AUTO_TEST_CASE(eth_pipeline_exception_maps_non_out_of_gas_bcos_exception)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    EthOrchestrationErrorPolicy errorPolicy;
    BOOST_REQUIRE_NO_THROW(invokePipelineException(errorPolicy, ctx, protocol::GasOverflow{}));

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

// GAP-003 E-PEX-05b: plain std::runtime_error currently escapes onPipelineException
// (characterization). CURRENT_ORACLE (observed): exception propagates — differs from
// PrecompiledError/GasOverflow path. GETH_ORACLE target: map to EVMC_INTERNAL_ERROR + Unknown
// without throw (see eth_pipeline_exception_maps_generic_exception).
BOOST_AUTO_TEST_CASE(eth_pipeline_exception_runtime_error_currently_propagates)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    EthOrchestrationErrorPolicy errorPolicy;
    bool caught = false;
    try
    {
        invokePipelineException(errorPolicy, ctx, std::runtime_error{"infrastructure fault"});
    }
    catch (std::runtime_error const& e)
    {
        caught = true;
        BOOST_CHECK_EQUAL(std::string{e.what()}, "infrastructure fault");
    }
    BOOST_CHECK(caught);
}

// GAP-003 E-PEX-06: policy handler swallows mapped BCOS exceptions (no escape to caller).
BOOST_AUTO_TEST_CASE(eth_pipeline_exception_handler_does_not_rethrow)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    EthOrchestrationErrorPolicy errorPolicy;
    BOOST_REQUIRE_NO_THROW(invokePipelineException(errorPolicy, ctx, protocol::PrecompiledError{}));
    BOOST_REQUIRE_NO_THROW(invokePipelineException(errorPolicy, ctx, protocol::GasOverflow{}));
    BOOST_REQUIRE_NO_THROW(invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{}));
}

// E-PEX-04: pipeline exception without checkpoint does not revert state.
BOOST_AUTO_TEST_CASE(eth_pipeline_exception_without_checkpoint_leaves_state)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_address sender{};
    sender.bytes[19] = 0x02;
    state::Account senderAccount;
    senderAccount.balance = 500;
    stateView.insert_account(sender, senderAccount);

    evmc_message message{};
    message.sender = sender;
    message.gas = 100'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    ctx.state.set_balance(sender, 250);
    BOOST_CHECK(!ctx.state.has_checkpoint());

    EthOrchestrationErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), 250);
}

// E-PCO-01: Eth pipeline-complete hook is a noop.
BOOST_AUTO_TEST_CASE(eth_pipeline_complete_is_noop)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = -5;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPipelineComplete(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, -5);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
}

// E-PEX-01: OutOfGas exception maps to OutOfGasLimit.
BOOST_AUTO_TEST_CASE(eth_pipeline_exception_maps_out_of_gas)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    EthOrchestrationErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

// E-PEX-03: pipeline exception reverts an open checkpoint.
BOOST_AUTO_TEST_CASE(eth_pipeline_exception_reverts_open_checkpoint)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_address sender{};
    sender.bytes[19] = 0x01;
    state::Account senderAccount;
    senderAccount.balance = 1'000;
    stateView.insert_account(sender, senderAccount);

    evmc_message message{};
    message.sender = sender;
    message.gas = 100'000;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, 100);
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), 100);
    BOOST_CHECK(ctx.state.has_checkpoint());

    EthOrchestrationErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), 1'000);
}

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

// E-PEN-04: top-level REVERT without authorization list stays reverted.
BOOST_AUTO_TEST_CASE(eth_post_execute_keeps_top_level_revert_without_auth_list)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 0;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    ctx.inputs.authorizationListPresent = false;

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(!ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::RevertInstruction));
}

// E-PEN-05: SUCCESS post-execute path is unchanged.
BOOST_AUTO_TEST_CASE(eth_post_execute_leaves_success_unchanged)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 0;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 42'000;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(!ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 42'000);
}

// E-PEN-06: INSUFFICIENT_BALANCE is not treated as included top-level vmerr.
BOOST_AUTO_TEST_CASE(eth_post_execute_keeps_insufficient_balance_at_top_level)
{
    state::test::InMemoryEvmStateReader stateView;

    evmc_message message{};
    message.depth = 0;

    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_INSUFFICIENT_BALANCE;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::NotEnoughCash);

    EthOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onPostExecuteNormalize(ctx);

    BOOST_CHECK(!ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::NotEnoughCash));
}

}  // namespace bcos::evm::test
