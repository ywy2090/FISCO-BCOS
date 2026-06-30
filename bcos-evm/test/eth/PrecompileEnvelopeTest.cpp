#define BOOST_TEST_MODULE PrecompileEnvelopeTest

#include "bcos-evm/eth/kernel/execution/CallTargetResolver.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address precompileAddr(uint8_t low)
{
    evmc_address a{};
    a.bytes[19] = low;
    return a;
}
}  // namespace

BOOST_AUTO_TEST_CASE(builtin_envelope_dispatches_ecrecover)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_CANCUN;

    evmc_address target = precompileAddr(0x01);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.gas = 500'000;

    execution::CallTargetDescriptor desc{.kind = execution::CallTargetKind::BuiltinPrecompile,
        .dispatchAddress = target,
        .warmPolicy = execution::WarmPolicy::TxEntryAlways,
        .routed = msg};

    auto out = precompiled::executePrecompileEnvelope({.state = state,
        .revision = cfg,
        .target = desc,
        .message = msg,
        .skipValueTransfer = false,
        .chainPort = nullptr});

    BOOST_CHECK(out.outcome == precompiled::PrecompileDispatchOutcome::Dispatched);
    BOOST_CHECK(out.result.status_code == EVMC_SUCCESS || out.result.status_code == EVMC_FAILURE);
}

BOOST_AUTO_TEST_CASE(builtin_envelope_dispatches_identity)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE};

    evmc_address target = precompileAddr(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.gas = 500'000;
    msg.input_data = inputBytes.data();
    msg.input_size = inputBytes.size();

    execution::CallTargetDescriptor desc{.kind = execution::CallTargetKind::BuiltinPrecompile,
        .dispatchAddress = target,
        .warmPolicy = execution::WarmPolicy::TxEntryAlways,
        .routed = msg};

    auto out = precompiled::executePrecompileEnvelope({.state = state,
        .revision = cfg,
        .target = desc,
        .message = msg,
        .skipValueTransfer = false,
        .chainPort = nullptr});

    BOOST_CHECK(out.outcome == precompiled::PrecompileDispatchOutcome::Dispatched);
    BOOST_CHECK_EQUAL(out.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(out.result.output_size, inputBytes.size());
}

BOOST_AUTO_TEST_CASE(empty_account_envelope_success_noop)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE};

    evmc_address target = precompileAddr(0x02);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.gas = 50'000;

    execution::CallTargetDescriptor desc{.kind = execution::CallTargetKind::EmptyAccount,
        .dispatchAddress = target,
        .warmPolicy = execution::WarmPolicy::Never,
        .routed = msg};

    auto out = precompiled::executeEmptyAccountEnvelope({.state = state,
        .revision = cfg,
        .target = desc,
        .message = msg,
        .skipValueTransfer = false,
        .chainPort = nullptr});

    BOOST_CHECK(out.outcome == precompiled::PrecompileDispatchOutcome::EmptyAccountSuccess);
    BOOST_CHECK_EQUAL(out.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(out.result.gas_left, msg.gas);
}

}  // namespace bcos::evm::test
