#define BOOST_TEST_MODULE TxPipelineTest

#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/apply/EthOrchestrationErrorPolicy.h"
#include "bcos-evm/eth/pipeline/CaptureSettlementSnapshot.h"
#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/EvmTxContextView.h"
#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <functional>
#include <stdexcept>
#include <type_traits>

namespace bcos::evm::test
{
namespace
{
TxPipelineContext makeContext(state::test::InMemoryStateView const& stateView)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    return TxPipelineContext{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(1)};
}

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

void wireMinimalEvmTxContextView(TxPipelineContext& ctx, EvmTxContextView& session)
{
    session.vm = ctx.inputs.vm;
    session.extension = ctx.extension;
    session.chainPort = ctx.chainPort;
    session.blockHashes = ctx.inputs.blockHashes;
    session.wire(ctx);
}

struct CallbackPrecheckPolicy : ChainPrecheckPolicy
{
    IntrinsicGasDebitParams intrinsicPolicy{};

    std::function<void(TxPipelineContext&)> onSetupMessage;
    std::function<void(TxPipelineContext&)> onCheckTransactionRules;
    std::function<void(TxPipelineContext&)> onCheckGasAffordable;
    std::function<void(TxPipelineContext&)> onCheckBalanceAndValue;
    std::function<void(ExecuteMessageInput&)> onTuneExecutionInput;
    std::function<ExecuteMessageOutput(ExecuteMessageInput&&)> onRunEvmExecution;

    IntrinsicGasDebitParams intrinsicGasDebitParams() const override { return intrinsicPolicy; }

    void pipelineSetupMessage(TxPipelineContext& ctx) const override
    {
        if (onSetupMessage)
        {
            onSetupMessage(ctx);
        }
    }

    void pipelineCheckRules(TxPipelineContext& ctx) const override
    {
        if (onCheckTransactionRules)
        {
            onCheckTransactionRules(ctx);
        }
    }

    void pipelineCheckGasAffordable(TxPipelineContext& ctx) const override
    {
        if (onCheckGasAffordable)
        {
            onCheckGasAffordable(ctx);
        }
    }

    void pipelineCheckBalance(TxPipelineContext& ctx) const override
    {
        if (onCheckBalanceAndValue)
        {
            onCheckBalanceAndValue(ctx);
        }
    }

    void pipelineTuneKernelInput(ExecuteMessageInput& input) const override
    {
        if (onTuneExecutionInput)
        {
            onTuneExecutionInput(input);
        }
    }

    ExecuteMessageOutput pipelineInvokeEvmKernel(ExecuteMessageInput&& input) const override
    {
        if (onRunEvmExecution)
        {
            return onRunEvmExecution(std::move(input));
        }
        return ChainPrecheckPolicy::pipelineInvokeEvmKernel(std::move(input));
    }
};

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
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    int checkGasAffordableCalls = 0;
    int checkBalanceAndValueCalls = 0;
    int tuneExecutionInputCalls = 0;
    CallbackPrecheckPolicy precheckPolicy;
    precheckPolicy.onCheckTransactionRules = [](TxPipelineContext& c) {
        c.earlyExit = true;
        evmc_result failResult{};
        failResult.status_code = EVMC_REJECTED;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::Unknown);
    };
    precheckPolicy.onCheckGasAffordable = [&](TxPipelineContext&) { ++checkGasAffordableCalls; };
    precheckPolicy.onCheckBalanceAndValue = [&](TxPipelineContext&) {
        ++checkBalanceAndValueCalls;
    };
    precheckPolicy.onTuneExecutionInput = [&](ExecuteMessageInput&) { ++tuneExecutionInputCalls; };

    EthOrchestrationErrorPolicy errorPolicy;
    stateTransitionExecute(ctx, precheckPolicy, errorPolicy);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::RulesRejected));
    BOOST_CHECK_EQUAL(checkGasAffordableCalls, 0);
    BOOST_CHECK_EQUAL(checkBalanceAndValueCalls, 0);
    BOOST_CHECK_EQUAL(tuneExecutionInputCalls, 0);
}

BOOST_AUTO_TEST_CASE(intrinsic_failure_maps_via_error_policy)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    bool mapped = false;
    CallbackPrecheckPolicy precheckPolicy;
    precheckPolicy.intrinsicPolicy.mode = IntrinsicDebitMode::AuthOnly;
    precheckPolicy.intrinsicPolicy.authorizationListPresent = true;
    precheckPolicy.intrinsicPolicy.authTupleCount = 2;
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

    stateTransitionExecute(ctx, precheckPolicy, errorPolicy);

    BOOST_CHECK(mapped);
    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::IntrinsicRejected));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

// GAP-006 characterization: checkBalanceAndValue failure defaults exitKind to GasAffordRejected
// even when evmc status is INSUFFICIENT_BALANCE (balance, not gas affordability).
// TxPipeline.cpp:89-94 assigns GasAffordRejected when earlyExit without explicit exitKind.
// CURRENT_ORACLE: GasAffordRejected + InsufficientFunds; GETH_ORACLE: ErrInsufficientFunds reject.
BOOST_AUTO_TEST_CASE(tx_check_balance_and_value_early_exit_skips_kernel_execution)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    int tuneExecutionInputCalls = 0;
    CallbackPrecheckPolicy precheckPolicy;
    precheckPolicy.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    precheckPolicy.onCheckBalanceAndValue = [](TxPipelineContext& c) {
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
        c.earlyExit = true;
        // Deliberately omit exitKind to exercise TxPipeline.cpp:92-94 default.
    };
    precheckPolicy.onTuneExecutionInput = [&](ExecuteMessageInput&) { ++tuneExecutionInputCalls; };

    EthOrchestrationErrorPolicy errorPolicy;
    stateTransitionExecute(ctx, precheckPolicy, errorPolicy);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::GasAffordRejected));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::InsufficientFunds));
    BOOST_CHECK_EQUAL(tuneExecutionInputCalls, 0);
}

// GAP-003 E-PEX-PIPE: full stateTransitionExecute path maps BCOS exception via
// EthOrchestrationErrorPolicy. GETH_ORACLE: go-ethereum/core/state_transition.go:550-552 —
// unexpected err rejects tx at block level. CURRENT_ORACLE: ExceptionHandled + EVMC_INTERNAL_ERROR
// + Unknown; no throw escapes stateTransitionExecute.
BOOST_AUTO_TEST_CASE(pipeline_generic_exception_maps_internal_error_eth_policy)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    CallbackPrecheckPolicy precheckPolicy;
    precheckPolicy.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    precheckPolicy.onSetupMessage = [](TxPipelineContext&) { throw protocol::PrecompiledError{}; };

    EthOrchestrationErrorPolicy errorPolicy;
    BOOST_REQUIRE_NO_THROW(stateTransitionExecute(ctx, precheckPolicy, errorPolicy));

    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::ExceptionHandled));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INTERNAL_ERROR);
    BOOST_CHECK_EQUAL(ctx.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::Unknown));
}

BOOST_AUTO_TEST_CASE(tx_check_balance_and_value_exception_maps_without_kernel_revert)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    bool mapCalled = false;
    ctx.state.checkpoint();
    CallbackPrecheckPolicy precheckPolicy;
    precheckPolicy.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    precheckPolicy.onCheckBalanceAndValue = [](TxPipelineContext&) {
        throw std::runtime_error("boom");
    };

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

    stateTransitionExecute(ctx, precheckPolicy, errorPolicy);

    BOOST_CHECK(mapCalled);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::ExceptionHandled));
    BOOST_CHECK(ctx.state.has_checkpoint());
}

BOOST_AUTO_TEST_CASE(capture_snapshot_non_eip7623_keeps_existing_values)
{
    state::test::InMemoryStateView stateView;
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
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    ctx.message.depth = 0;
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    EvmTxContextView session;
    wireMinimalEvmTxContextView(ctx, session);

    CallbackPrecheckPolicy precheckPolicy;
    precheckPolicy.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    precheckPolicy.onRunEvmExecution = [](ExecuteMessageInput&&) -> ExecuteMessageOutput {
        ExecuteMessageOutput output;
        evmc_result raw{};
        raw.status_code = EVMC_INVALID_INSTRUCTION;
        raw.gas_left = 90'000;
        output.result = evmc::Result(raw);
        return output;
    };

    EthOrchestrationErrorPolicy errorPolicy;
    stateTransitionExecute(ctx, precheckPolicy, errorPolicy);

    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(TxPipelineExitKind::Completed));
    BOOST_CHECK(ctx.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
}

BOOST_AUTO_TEST_CASE(pipeline_passes_ctx_state_pointer_to_execute_message)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    ctx.message.sender = addressFromLastByte(0x01);
    ctx.message.recipient = addressFromLastByte(0x02);
    ctx.message.code_address = ctx.message.recipient;
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    EvmTxContextView session;
    wireMinimalEvmTxContextView(ctx, session);

    auto const warmAddr = ctx.message.recipient;
    ctx.state.pin_warm_create_address(warmAddr);
    BOOST_REQUIRE(ctx.state.is_address_warm(warmAddr));

    state::State* capturedState = nullptr;
    CallbackPrecheckPolicy precheckPolicy;
    precheckPolicy.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    precheckPolicy.onRunEvmExecution = [&](ExecuteMessageInput&& execInput) {
        capturedState = execInput.state;
        BOOST_CHECK(execInput.state == &ctx.state);
        ExecuteMessageOutput output;
        evmc_result raw{};
        raw.status_code = EVMC_SUCCESS;
        raw.gas_left = execInput.message.gas;
        output.result = evmc::Result(raw);
        return output;
    };

    EthOrchestrationErrorPolicy errorPolicy;
    stateTransitionExecute(ctx, precheckPolicy, errorPolicy);

    BOOST_REQUIRE(capturedState != nullptr);
    BOOST_CHECK(capturedState == &ctx.state);
    BOOST_CHECK(capturedState->is_address_warm(warmAddr));
}

}  // namespace bcos::evm::test
