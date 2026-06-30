#define BOOST_TEST_MODULE OpStackStateTransitionBindingsTest

#include "bcos-evm/opstack/OpStackStateTransitionBindings.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <algorithm>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bytesConstRef toRef(bytes const& data)
{
    return {data.data(), data.size()};
}
}  // namespace

BOOST_AUTO_TEST_CASE(intrinsic_policy_op_stack_entry)
{
    OpStackExecutionRequest input;
    state::test::InMemoryStateView stateView;
    evmc_message msg{};
    StateTransitionContext ctx{stateView, msg, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    OpStackStateTransitionBindings::Context bindingsCtx{input, view};
    auto policy = OpStackStateTransitionBindings::buildStateTransitionHooks(bindingsCtx);

    BOOST_CHECK_EQUAL(static_cast<int>(policy.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::OpStackEntry));
}

BOOST_AUTO_TEST_CASE(pre_debit_entry_floor_rejects)
{
    bytes data(100, 0xff);
    auto const floor = floorDataGas(toRef(data));

    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x71);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});

    evmc_message message{};
    message.sender = sender;
    message.input_data = data.data();
    message.input_size = data.size();
    message.gas = static_cast<int64_t>(floor - 1);

    OpStackExecutionRequest input;
    input.skipTransactionChecks = false;

    StateTransitionContext ctx{stateView, message, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    OpStackStateTransitionBindings::Context bindingsCtx{input, view};
    auto policy = OpStackStateTransitionBindings::buildStateTransitionHooks(bindingsCtx);
    policy.onPreCheckGasAffordable(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(bind_returns_precheck_policy_and_error_policy)
{
    OpStackExecutionRequest input;
    state::test::InMemoryStateView stateView;
    evmc_message msg{};
    StateTransitionContext ctx{stateView, msg, input.revisionConfig, bcos::u256(0)};
    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    OpStackStateTransitionBindings::Context bindingsCtx{input, view};
    auto bindings = OpStackStateTransitionBindings::bind(bindingsCtx);

    BOOST_CHECK_EQUAL(static_cast<int>(bindings.hooks.getIntrinsicGasParams().mode),
        static_cast<int>(IntrinsicDebitMode::OpStackEntry));
}

}  // namespace bcos::evm::test
