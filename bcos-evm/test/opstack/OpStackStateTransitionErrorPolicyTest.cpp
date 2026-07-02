#define BOOST_TEST_MODULE OpStackStateTransitionErrorPolicyTest

#include "bcos-evm/opstack/apply/OpStackStateTransitionErrorPolicy.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/core/StateTransitionHooks.h"
#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionExecute.h"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <stdexcept>

namespace bcos::evm::test
{
namespace
{
template <typename Exception>
void invokePipelineException(
    StateTransitionErrorPolicy const& errorPolicy, StateTransitionContext& ctx, Exception exception)
{
    try
    {
        throw exception;
    }
    catch (...)
    {
        errorPolicy.onException(ctx, std::current_exception());
    }
}
}  // namespace

// O-IGF-01: intrinsic failure maps to OutOfGasLimit regardless of failure kind.
BOOST_AUTO_TEST_CASE(opstack_intrinsic_gas_failure_maps_to_out_of_gas_limit)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 21'000;

    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackStateTransitionErrorPolicy errorPolicy;
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::OpStackIntrinsicOutOfGas);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

// O-IGF-02: failure reason is ignored (same result for GasLimitMinimum).
BOOST_AUTO_TEST_CASE(opstack_intrinsic_gas_failure_ignores_failure_kind)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.gas = 21'000;

    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackStateTransitionErrorPolicy errorPolicy;
    errorPolicy.onIntrinsicGasFailure(ctx, IntrinsicDebitFailure::GasLimitMinimum);

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

// O-PEX-01 / O-PEX-02: any pipeline exception maps to internal error.
BOOST_AUTO_TEST_CASE(opstack_pipeline_exception_maps_to_internal_error)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackStateTransitionErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, std::runtime_error{"boom"});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

BOOST_AUTO_TEST_CASE(opstack_pipeline_exception_treats_out_of_gas_like_generic)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    OpStackStateTransitionErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

// O-PEX-03: pipeline exception reverts an open checkpoint.
BOOST_AUTO_TEST_CASE(opstack_pipeline_exception_reverts_open_checkpoint)
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

    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    ctx.state.checkpoint();
    ctx.state.set_balance(sender, 200);
    BOOST_CHECK(ctx.state.has_checkpoint());

    OpStackStateTransitionErrorPolicy errorPolicy;
    invokePipelineException(errorPolicy, ctx, protocol::OutOfGas{});

    BOOST_CHECK(!ctx.state.has_checkpoint());
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), 2'000);
}

// O-PEN-01: included top-level vmerr normalizes status_code for settlement (ADR-015).
BOOST_AUTO_TEST_CASE(opstack_post_execute_normalizes_included_top_level_vmerr)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.depth = 0;

    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_INVALID_INSTRUCTION;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::Unknown);
    ctx.logs.push_back(state::LogEntry{});

    OpStackStateTransitionErrorPolicy errorPolicy;
    errorPolicy.onFinalizeGasUsed(ctx);

    BOOST_CHECK(ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::BadInstruction));
    BOOST_CHECK_EQUAL(ctx.logs.size(), 1U);
}

BOOST_AUTO_TEST_CASE(opstack_post_execute_normalizes_stack_overflow_for_settlement)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.depth = 0;

    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_STACK_OVERFLOW;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::Unknown);

    OpStackStateTransitionErrorPolicy errorPolicy;
    errorPolicy.onFinalizeGasUsed(ctx);

    BOOST_CHECK(ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfStack));
}

BOOST_AUTO_TEST_CASE(opstack_post_execute_keeps_top_level_revert_without_auth_list)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.depth = 0;

    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);

    OpStackStateTransitionErrorPolicy errorPolicy;
    errorPolicy.onFinalizeGasUsed(ctx);

    BOOST_CHECK(!ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_REVERT);
}

// O-PCO-01: OpStack inherits base noop pipeline-complete hook.
BOOST_AUTO_TEST_CASE(opstack_pipeline_complete_is_noop)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = -5;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);

    OpStackStateTransitionErrorPolicy errorPolicy;
    errorPolicy.onComplete(ctx);

    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, -5);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
}

// INT-03: stateTransitionExecute routes OpStack intrinsic failures through error policy.
BOOST_AUTO_TEST_CASE(opstack_intrinsic_failure_via_run_tx_pipeline)
{
    state::test::InMemoryStateView stateView;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 1;

    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    struct OpStackEntryStateTransitionHooks : StateTransitionHooks
    {
        DeductIntrinsicGasParams getIntrinsicGasParams() const override
        {
            DeductIntrinsicGasParams policy;
            policy.mode = IntrinsicDebitMode::OpStackEntry;
            return policy;
        }
    };

    OpStackEntryStateTransitionHooks hooks;

    OpStackStateTransitionErrorPolicy errorPolicy;
    stateTransitionExecute(ctx, hooks, errorPolicy);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.exitKind),
        static_cast<int>(StateTransitionExitKind::IntrinsicRejected));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
}

}  // namespace bcos::evm::test
