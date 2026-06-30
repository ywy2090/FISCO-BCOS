#define BOOST_TEST_MODULE EvmTxContextViewTest

#include "bcos-evm/eth/pipeline/EvmTxContextView.h"
#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include "helpers/InMemoryEvmStateReader.h"
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

BOOST_AUTO_TEST_CASE(toExecuteMessageInput_matches_legacy_build_fields)
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_message message{};
    message.gas = 50'000;
    TxPipelineContext ctx{stateView, message, bcos::evm_standard::RevisionConfig{}, bcos::u256(3)};
    evmc::VM vm{evmc_create_evmone()};
    populateContext(ctx, vm);

    auto session = makeTxContextView(vm);
    session.wire(ctx);

    auto const fromSession = session.toExecuteMessageInput(ctx);
    auto const fromLegacy = buildExecuteMessageInput(ctx);

    BOOST_CHECK(fromSession.state == fromLegacy.state);
    BOOST_CHECK(fromSession.vm == fromLegacy.vm);
    BOOST_CHECK_EQUAL(fromSession.message.gas, fromLegacy.message.gas);
    BOOST_CHECK(fromSession.gasPrice == fromLegacy.gasPrice);
    BOOST_CHECK(fromSession.blockInfo.number == fromLegacy.blockInfo.number);
    BOOST_CHECK(fromSession.revisionConfig.revision == fromLegacy.revisionConfig.revision);
    BOOST_CHECK(fromSession.extension == fromLegacy.extension);
    BOOST_CHECK(fromSession.chainPort == fromLegacy.chainPort);
    BOOST_CHECK_EQUAL(fromSession.fixStorageStatus, fromLegacy.fixStorageStatus);
    BOOST_CHECK_EQUAL(fromSession.fixNonceInit, fromLegacy.fixNonceInit);
}

BOOST_AUTO_TEST_CASE(wire_sets_session_pointer_on_context)
{
    state::test::InMemoryEvmStateReader stateView;
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
