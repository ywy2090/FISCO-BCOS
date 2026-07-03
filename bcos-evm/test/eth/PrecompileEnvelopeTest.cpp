#define BOOST_TEST_MODULE PrecompileEnvelopeTest

#include "bcos-evm/eth/core/CallTargetTypes.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "bcos-evm/eth/state/State.hpp"
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
    bcos::evm::RevisionConfig cfg{};
    cfg.revision = EVMC_CANCUN;

    evmc_address target = precompileAddr(0x01);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.gas = 500'000;

    execution::ClassifiedCallTarget desc{.route = execution::CallTargetRoute::BuiltinPrecompile,
        .dispatchAddress = target,
        .accessWarm = execution::AccessWarmSchedule::AtTxPrepare,
        .routed = msg};

    auto out = precompiled::executePrecompileEnvelope({.state = state,
        .revision = cfg,
        .target = desc,
        .message = msg,
        .skipValueTransfer = false,
        .callTargetPort = nullptr});

    BOOST_CHECK(out.route == precompiled::PrecompileEnvelopeRoute::Precompile);
    BOOST_CHECK(out.result.status_code == EVMC_SUCCESS || out.result.status_code == EVMC_FAILURE);
}

BOOST_AUTO_TEST_CASE(builtin_envelope_dispatches_identity)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE};

    evmc_address target = precompileAddr(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.gas = 500'000;
    msg.input_data = inputBytes.data();
    msg.input_size = inputBytes.size();

    execution::ClassifiedCallTarget desc{.route = execution::CallTargetRoute::BuiltinPrecompile,
        .dispatchAddress = target,
        .accessWarm = execution::AccessWarmSchedule::AtTxPrepare,
        .routed = msg};

    auto out = precompiled::executePrecompileEnvelope({.state = state,
        .revision = cfg,
        .target = desc,
        .message = msg,
        .skipValueTransfer = false,
        .callTargetPort = nullptr});

    BOOST_CHECK(out.route == precompiled::PrecompileEnvelopeRoute::Precompile);
    BOOST_CHECK_EQUAL(out.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(out.result.output_size, inputBytes.size());
}

BOOST_AUTO_TEST_CASE(empty_account_envelope_success_noop)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE};

    evmc_address target = precompileAddr(0x02);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.gas = 50'000;

    execution::ClassifiedCallTarget desc{.route = execution::CallTargetRoute::EmptyAccount,
        .dispatchAddress = target,
        .accessWarm = execution::AccessWarmSchedule::AtFirstAccess,
        .routed = msg};

    auto out = precompiled::executeEmptyAccountEnvelope({.state = state,
        .revision = cfg,
        .target = desc,
        .message = msg,
        .skipValueTransfer = false,
        .callTargetPort = nullptr});

    BOOST_CHECK(out.route == precompiled::PrecompileEnvelopeRoute::EmptyAccount);
    BOOST_CHECK_EQUAL(out.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(out.result.gas_left, msg.gas);
}

}  // namespace bcos::evm::test
