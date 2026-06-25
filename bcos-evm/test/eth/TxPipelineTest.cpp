#define BOOST_TEST_MODULE TxPipelineTest

#include "bcos-evm/eth/orchestration/TxPipeline.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/orchestration/CaptureSettlementSnapshot.h"
#include "bcos-evm/eth/orchestration/EthOrchestrationErrorPolicy.h"
#include "bcos-evm/eth/orchestration/OrchestrationErrorPolicy.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <functional>
#include <stdexcept>
#include <type_traits>

namespace bcos::evm::test
{
namespace
{
TxPipelineContext makeContext(state::test::InMemoryEvmStateReader const& stateView)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    return TxPipelineContext{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(1)};
}

struct CallbackOrchestrationErrorPolicy : OrchestrationErrorPolicy
{
    std::function<void(TxPipelineContext&, IntrinsicDebitFailure)> onIntrinsic;
    std::function<void(TxPipelineContext&, std::exception_ptr)> onException;

    void onIntrinsicGasFailure(TxPipelineContext& ctx, IntrinsicDebitFailure failure) const override
    {
        if (onIntrinsic)
        {
            onIntrinsic(ctx, failure);
        }
    }

    void onPipelineException(TxPipelineContext& ctx, std::exception_ptr exceptionPtr) const override
    {
        if (onException)
        {
            onException(ctx, exceptionPtr);
        }
    }
};
}  // namespace

static_assert(!std::is_default_constructible_v<TxPipelineContext>);
static_assert(!std::is_copy_constructible_v<TxPipelineContext>);
static_assert(!std::is_move_constructible_v<TxPipelineContext>);

BOOST_AUTO_TEST_CASE(tx_check_transaction_rules_early_exit_skips_later_hooks)
{
    state::test::InMemoryEvmStateReader stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    int checkGasAffordableCalls = 0;
    int checkBalanceAndValueCalls = 0;
    int tuneExecutionInputCalls = 0;
    TxPipelineHooks hooks;
    hooks.txCheckTransactionRules = [](TxPipelineContext& c) {
        c.earlyExit = true;
        evmc_result failResult{};
        failResult.status_code = EVMC_REJECTED;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::Unknown);
    };
    hooks.txCheckGasAffordable = [&](TxPipelineContext&) { ++checkGasAffordableCalls; };
    hooks.txCheckBalanceAndValue = [&](TxPipelineContext&) { ++checkBalanceAndValueCalls; };
    hooks.txTuneExecutionInput = [&](ExecuteMessageInput&) { ++tuneExecutionInputCalls; };

    EthOrchestrationErrorPolicy errorPolicy;
    runTxPipeline(ctx, hooks, errorPolicy);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::RulesRejected));
    BOOST_CHECK_EQUAL(checkGasAffordableCalls, 0);
    BOOST_CHECK_EQUAL(checkBalanceAndValueCalls, 0);
    BOOST_CHECK_EQUAL(tuneExecutionInputCalls, 0);
}

BOOST_AUTO_TEST_CASE(intrinsic_failure_maps_via_error_policy)
{
    state::test::InMemoryEvmStateReader stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    bool mapped = false;
    TxPipelineHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::AuthOnly;
    hooks.intrinsicPolicy.authorizationListPresent = true;
    hooks.intrinsicPolicy.authTupleCount = 2;
    ctx.message.gas = 1;

    CallbackOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onIntrinsic = [&](TxPipelineContext& c, IntrinsicDebitFailure failure) {
        mapped = true;
        BOOST_CHECK_EQUAL(
            static_cast<int>(failure), static_cast<int>(IntrinsicDebitFailure::AuthTupleOutOfGas));
        evmc_result failResult{};
        failResult.status_code = EVMC_OUT_OF_GAS;
        failResult.gas_left = 0;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
    };

    runTxPipeline(ctx, hooks, errorPolicy);

    BOOST_CHECK(mapped);
    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::IntrinsicRejected));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(tx_check_balance_and_value_early_exit_skips_kernel_execution)
{
    state::test::InMemoryEvmStateReader stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    int tuneExecutionInputCalls = 0;
    TxPipelineHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    hooks.txCheckBalanceAndValue = [](TxPipelineContext& c) {
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
        c.earlyExit = true;
        c.exitKind = TxPipelineExitKind::GasAffordRejected;
    };
    hooks.txTuneExecutionInput = [&](ExecuteMessageInput&) { ++tuneExecutionInputCalls; };

    EthOrchestrationErrorPolicy errorPolicy;
    runTxPipeline(ctx, hooks, errorPolicy);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::GasAffordRejected));
    BOOST_CHECK_EQUAL(tuneExecutionInputCalls, 0);
}

BOOST_AUTO_TEST_CASE(tx_check_balance_and_value_exception_maps_without_kernel_revert)
{
    state::test::InMemoryEvmStateReader stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    bool mapCalled = false;
    ctx.state.checkpoint();
    TxPipelineHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    hooks.txCheckBalanceAndValue = [](TxPipelineContext&) { throw std::runtime_error("boom"); };

    CallbackOrchestrationErrorPolicy errorPolicy;
    errorPolicy.onException = [&](TxPipelineContext& c, std::exception_ptr ex) {
        mapCalled = true;
        BOOST_REQUIRE(ex != nullptr);
        try
        {
            std::rethrow_exception(ex);
        }
        catch (std::runtime_error const& err)
        {
            BOOST_CHECK_EQUAL(std::string(err.what()), "boom");
        }
        evmc_result failResult{};
        failResult.status_code = EVMC_INTERNAL_ERROR;
        failResult.gas_left = 0;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::Unknown);
    };

    runTxPipeline(ctx, hooks, errorPolicy);

    BOOST_CHECK(mapCalled);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::ExceptionHandled));
    BOOST_CHECK(ctx.state.has_checkpoint());
}

BOOST_AUTO_TEST_CASE(capture_snapshot_non_eip7623_keeps_existing_values)
{
    state::test::InMemoryEvmStateReader stateView;
    auto ctx = makeContext(stateView);
    ctx.intrinsicDebitMode = IntrinsicDebitMode::None;
    ctx.snapshot.gasLimit = 123;
    ctx.snapshot.evmGasRefund = 456;

    ExecuteMessageOutput kernelOutput;
    kernelOutput.gasRefund = 789;

    captureSettlementSnapshot(ctx, kernelOutput);

    BOOST_CHECK_EQUAL(ctx.snapshot.gasLimit, 123);
    BOOST_CHECK_EQUAL(ctx.snapshot.evmGasRefund, 456);
}

// INT-04: completed pipeline path invokes Eth post-execute normalization.
BOOST_AUTO_TEST_CASE(completed_path_invokes_eth_post_execute_normalize)
{
    state::test::InMemoryEvmStateReader stateView;
    auto ctx = makeContext(stateView);
    ctx.message.depth = 0;
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    TxPipelineHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    hooks.txRunEvmExecutionOverride = [](ExecuteMessageInput&&) -> ExecuteMessageOutput {
        ExecuteMessageOutput output;
        evmc_result raw{};
        raw.status_code = EVMC_INVALID_INSTRUCTION;
        raw.gas_left = 90'000;
        output.result = evmc::Result(raw);
        return output;
    };

    EthOrchestrationErrorPolicy errorPolicy;
    runTxPipeline(ctx, hooks, errorPolicy);

    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::Completed));
    BOOST_CHECK(ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
}

}  // namespace bcos::evm::test
