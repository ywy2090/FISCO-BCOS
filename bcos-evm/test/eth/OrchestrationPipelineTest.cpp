#define BOOST_TEST_MODULE OrchestrationPipelineTest

#include "bcos-evm/eth/orchestration/OrchestrationPipeline.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/orchestration/captureSettlementSnapshot.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <stdexcept>
#include <type_traits>

namespace bcos::evm::test
{
namespace
{
OrchestrationContext makeContext(state::test::InMemoryStateView const& stateView)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    return OrchestrationContext{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(1)};
}
}  // namespace

static_assert(!std::is_default_constructible_v<OrchestrationContext>);
static_assert(!std::is_copy_constructible_v<OrchestrationContext>);
static_assert(!std::is_move_constructible_v<OrchestrationContext>);

BOOST_AUTO_TEST_CASE(pre_execute_early_exit_skips_later_hooks)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    int preDebitCalls = 0;
    int preKernelCalls = 0;
    int tuneKernelCalls = 0;
    OrchestrationHooks hooks;
    hooks.preExecute = [](OrchestrationContext& c) {
        c.earlyExit = true;
        evmc_result failResult{};
        failResult.status_code = EVMC_REJECTED;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::Unknown);
    };
    hooks.preDebitEntry = [&](OrchestrationContext&) { ++preDebitCalls; };
    hooks.preKernel = [&](OrchestrationContext&) { ++preKernelCalls; };
    hooks.tuneKernelInput = [&](ExecuteMessageInput&) { ++tuneKernelCalls; };

    runOrchestration(ctx, hooks);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.exitKind),
        static_cast<int>(OrchestrationExitKind::PreExecuteRejected));
    BOOST_CHECK_EQUAL(preDebitCalls, 0);
    BOOST_CHECK_EQUAL(preKernelCalls, 0);
    BOOST_CHECK_EQUAL(tuneKernelCalls, 0);
}

BOOST_AUTO_TEST_CASE(intrinsic_failure_maps_via_hook)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    bool mapped = false;
    OrchestrationHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::AuthOnly;
    hooks.intrinsicPolicy.authorizationListPresent = true;
    hooks.intrinsicPolicy.authTupleCount = 2;
    ctx.message.gas = 1;
    hooks.mapIntrinsicFailure = [&](OrchestrationContext& c, IntrinsicDebitFailure failure) {
        mapped = true;
        BOOST_CHECK_EQUAL(
            static_cast<int>(failure), static_cast<int>(IntrinsicDebitFailure::AuthTupleOutOfGas));
        evmc_result failResult{};
        failResult.status_code = EVMC_OUT_OF_GAS;
        failResult.gas_left = 0;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
    };

    runOrchestration(ctx, hooks);

    BOOST_CHECK(mapped);
    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(OrchestrationExitKind::IntrinsicRejected));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(pre_kernel_early_exit_skips_kernel_execution)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    int tuneKernelCalls = 0;
    OrchestrationHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    hooks.preKernel = [](OrchestrationContext& c) {
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        c.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
        c.earlyExit = true;
        c.exitKind = OrchestrationExitKind::PreDebitRejected;
    };
    hooks.tuneKernelInput = [&](ExecuteMessageInput&) { ++tuneKernelCalls; };

    runOrchestration(ctx, hooks);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(OrchestrationExitKind::PreDebitRejected));
    BOOST_CHECK_EQUAL(tuneKernelCalls, 0);
}

BOOST_AUTO_TEST_CASE(pre_kernel_exception_maps_without_kernel_revert)
{
    state::test::InMemoryStateView stateView;
    auto ctx = makeContext(stateView);
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    bool mapCalled = false;
    ctx.state.checkpoint();
    OrchestrationHooks hooks;
    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    hooks.preKernel = [](OrchestrationContext&) { throw std::runtime_error("boom"); };
    hooks.mapException = [&](OrchestrationContext& c, std::exception_ptr ex) {
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

    runOrchestration(ctx, hooks);

    BOOST_CHECK(mapCalled);
    BOOST_CHECK_EQUAL(
        static_cast<int>(ctx.exitKind), static_cast<int>(OrchestrationExitKind::ExceptionMapped));
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

}  // namespace bcos::evm::test
