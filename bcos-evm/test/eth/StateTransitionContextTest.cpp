#define BOOST_TEST_MODULE StateTransitionContextTest

#include "bcos-evm/eth/state-transition/StateTransitionContext.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
void populateContext(StateTransitionContext& ctx, evmc::VM& vm)
{
    ctx.inputs.blockInfo.number = 42;
    ctx.inputs.blockHashes = [](int64_t n) {
        evmc_bytes32 out{};
        out.bytes[31] = static_cast<uint8_t>(n);
        return out;
    };
    ctx.revisionConfig.revision = EVMC_CANCUN;
    ctx.gasPrice = 7;
    ctx.wireExecutionEnvironment(&vm, nullptr, nullptr);
}
}  // namespace

BOOST_AUTO_TEST_CASE(toInnerExecuteInput_projects_context_fields)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    message.gas = 50'000;
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(3)};
    evmc::VM vm{evmc_create_evmone()};
    populateContext(ctx, vm);

    auto const input = ctx.toInnerExecuteInput();

    BOOST_CHECK(input.state == &ctx.state);
    BOOST_CHECK(input.vm == &vm);
    BOOST_CHECK_EQUAL(input.message.gas, 50'000);
    BOOST_CHECK(input.gasPrice == bcos::u256(7));
    BOOST_CHECK(input.blockInfo.number == 42);
    BOOST_CHECK(input.revisionConfig.revision == EVMC_CANCUN);
    BOOST_CHECK(input.extension == ctx.extension);
    BOOST_CHECK(input.chainPort == ctx.chainPort);
}

BOOST_AUTO_TEST_CASE(wireExecutionEnvironment_sets_vm_and_ports)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    StateTransitionContext ctx{
        stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    evmc::VM vm{evmc_create_evmone()};

    ctx.wireExecutionEnvironment(&vm, nullptr, nullptr);

    BOOST_CHECK_EQUAL(ctx.inputs.vm, &vm);
    BOOST_CHECK(ctx.extension == nullptr);
    BOOST_CHECK(ctx.chainPort == nullptr);
}

}  // namespace bcos::evm::test
