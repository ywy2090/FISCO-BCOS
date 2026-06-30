#define BOOST_TEST_MODULE EvmTxContextViewTest

#include "bcos-evm/eth/pipeline/EvmTxContextView.h"
#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
void populateContext(TxPipelineContext& ctx, evmc::VM& vm)
{
    ctx.inputs.vm = &vm;
    ctx.inputs.blockInfo.number = 42;
    ctx.inputs.blockHashes = [](int64_t n) {
        evmc_bytes32 out{};
        out.bytes[31] = static_cast<uint8_t>(n);
        return out;
    };
    ctx.revisionConfig.revision = EVMC_CANCUN;
    ctx.gasPrice = 7;
}

EvmTxContextView makeTxContextView(evmc::VM& vm)
{
    EvmTxContextView session;
    session.vm = &vm;
    session.blockHashes = [](int64_t) { return evmc_bytes32{}; };
    return session;
}
}  // namespace

BOOST_AUTO_TEST_CASE(toExecuteMessageInput_projects_wired_context_fields)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    message.gas = 50'000;
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(3)};
    evmc::VM vm{evmc_create_evmone()};
    populateContext(ctx, vm);

    auto session = makeTxContextView(vm);
    session.wire(ctx);

    auto const input = session.toExecuteMessageInput(ctx);

    BOOST_CHECK(input.state == &ctx.state);
    BOOST_CHECK(input.vm == &vm);
    BOOST_CHECK_EQUAL(input.message.gas, 50'000);
    BOOST_CHECK(input.gasPrice == bcos::u256(7));
    BOOST_CHECK(input.blockInfo.number == 42);
    BOOST_CHECK(input.revisionConfig.revision == EVMC_CANCUN);
    BOOST_CHECK(input.extension == session.extension);
    BOOST_CHECK(input.chainPort == session.chainPort);
}

BOOST_AUTO_TEST_CASE(wire_sets_session_pointer_on_context)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(0)};
    evmc::VM vm{evmc_create_evmone()};
    ctx.inputs.vm = &vm;

    auto session = makeTxContextView(vm);
    session.wire(ctx);

    BOOST_CHECK(ctx.txContextView == &session);
    BOOST_CHECK_EQUAL(ctx.inputs.vm, &vm);
}

}  // namespace bcos::evm::test
