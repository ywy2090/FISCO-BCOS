#define BOOST_TEST_MODULE FiscoChainCallTargetAdapterTest

#include "bcos-evm/bcos/FiscoChainCallTargetAdapter.h"
#include "bcos-evm/eth/core/CallTargetKind.h"
#include "bcos-evm/eth/core/FrameScope.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos/adapters/InMemoryChainPrecompileAdapter.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{

evmc_address addressFromValue(uint64_t value)
{
    evmc_address address{};
    for (int i = 19; i >= 0 && value > 0; --i)
    {
        address.bytes[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8U;
    }
    return address;
}

evmc_address fiscoPrecompileAddress(uint16_t suffix)
{
    evmc_address addr{};
    addr.bytes[18] = static_cast<uint8_t>((suffix >> 8U) & 0xFFU);
    addr.bytes[19] = static_cast<uint8_t>(suffix & 0xFFU);
    return addr;
}

}  // namespace

BOOST_AUTO_TEST_CASE(classify_and_dispatch_0x1003_hit)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    auto const target = addressFromValue(0x1003);

    bool dispatched = false;
    InMemoryChainPrecompileAdapter dispatchPort(
        [&](evmc_revision /*rev*/, evmc_message const& message) -> std::optional<evmc_result> {
            dispatched = true;
            BOOST_CHECK_EQUAL(
                std::memcmp(message.recipient.bytes, target.bytes, sizeof(target.bytes)), 0);
            evmc_result result{};
            result.status_code = EVMC_SUCCESS;
            result.gas_left = message.gas;
            return result;
        });

    FiscoChainCallTargetAdapter adapter(state, dispatchPort);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 50'000;
    msg.recipient = target;
    msg.code_address = target;

    auto desc = adapter.classifyTarget(state, target, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(desc.has_value());
    BOOST_CHECK(desc->kind == execution::CallTargetKind::ChainPrecompile);
    BOOST_CHECK(std::memcmp(desc->dispatchAddress.bytes, target.bytes, sizeof(target.bytes)) == 0);

    auto result = adapter.dispatch(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(dispatched);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(null_dispatch_port_skips_routing)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    InMemoryChainPrecompileAdapter dispatchPort;
    FiscoChainCallTargetAdapter adapter(state, dispatchPort);

    evmc_message msg{};
    msg.recipient = fiscoPrecompileAddress(0x1003);
    msg.code_address = msg.recipient;

    auto desc = adapter.classifyTarget(state, msg.recipient, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(desc.has_value());
    BOOST_CHECK(!adapter.dispatch(EVMC_CANCUN, msg).has_value());
}

BOOST_AUTO_TEST_CASE(dispatch_port_invoked_for_fisco_address)
{
    bool invoked = false;
    InMemoryChainPrecompileAdapter dispatchPort(
        [&invoked](
            evmc_revision /*rev*/, evmc_message const& message) -> std::optional<evmc_result> {
            invoked = true;
            evmc_result raw{};
            raw.status_code = EVMC_SUCCESS;
            raw.gas_left = message.gas;
            return raw;
        });

    state::test::InMemoryStateView base;
    state::State state{base};
    FiscoChainCallTargetAdapter adapter(state, dispatchPort);

    evmc_message msg{};
    msg.gas = 50'000;
    msg.recipient = fiscoPrecompileAddress(0x1003);
    msg.code_address = msg.recipient;

    auto result = adapter.dispatch(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(invoked);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(fisco_precompile_dispatch_uses_callback_for_0x1000_plus)
{
    bool called = false;
    InMemoryChainPrecompileAdapter dispatchPort(
        [&called](
            evmc_revision /*rev*/, const evmc_message& message) -> std::optional<evmc_result> {
            called = true;
            evmc_result result{};
            result.status_code = EVMC_SUCCESS;
            result.gas_left = message.gas - 123;
            return result;
        });

    state::test::InMemoryStateView base;
    state::State state{base};
    FiscoChainCallTargetAdapter adapter(state, dispatchPort);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 50000;
    msg.sender = addressFromValue(0x01);
    msg.recipient = addressFromValue(0x1000);
    msg.code_address = msg.recipient;

    auto desc = adapter.classifyTarget(state, msg.recipient, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(desc.has_value());
    auto result = adapter.dispatch(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(called);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result->gas_left, msg.gas - 123);
}

BOOST_AUTO_TEST_CASE(fisco_precompile_dispatch_returns_nullopt_for_below_0x1000)
{
    bool called = false;
    InMemoryChainPrecompileAdapter dispatchPort(
        [&called](
            evmc_revision /*rev*/, const evmc_message& /*message*/) -> std::optional<evmc_result> {
            called = true;
            return evmc_result{};
        });

    state::test::InMemoryStateView base;
    state::State state{base};
    FiscoChainCallTargetAdapter adapter(state, dispatchPort);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.sender = addressFromValue(0x01);
    msg.recipient = addressFromValue(0x0FFF);
    msg.code_address = msg.recipient;

    BOOST_CHECK(!adapter.classifyTarget(state, msg.recipient, msg, execution::FrameScope::TopLevel)
                     .has_value());
    BOOST_CHECK(!adapter.dispatch(EVMC_CANCUN, msg).has_value());
    BOOST_CHECK(!called);
}

BOOST_AUTO_TEST_CASE(dynamic_precompile_marker_is_resolved_by_adapter)
{
    state::test::InMemoryStateView baseView;
    state::State state(baseView);
    auto markerContract = addressFromValue(0x2222);
    auto expectedTarget = addressFromValue(0x1003);
    auto markerCode = std::string("[PRECOMPILED],0000000000000000000000000000000000001003");
    state.set_code(markerContract, bcos::bytes(markerCode.begin(), markerCode.end()), {});

    bool called = false;
    InMemoryChainPrecompileAdapter dispatchPort(
        [&called, expectedTarget](
            evmc_revision /*rev*/, const evmc_message& message) -> std::optional<evmc_result> {
            called = true;
            BOOST_CHECK_EQUAL(std::memcmp(message.code_address.bytes, expectedTarget.bytes,
                                  sizeof(expectedTarget.bytes)),
                0);
            BOOST_CHECK_EQUAL(std::memcmp(message.recipient.bytes, expectedTarget.bytes,
                                  sizeof(expectedTarget.bytes)),
                0);
            evmc_result result{};
            result.status_code = EVMC_SUCCESS;
            result.gas_left = message.gas;
            return result;
        });

    FiscoChainCallTargetAdapter adapter(state, dispatchPort);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 50000;
    msg.sender = addressFromValue(0x01);
    msg.recipient = markerContract;
    msg.code_address = markerContract;

    auto desc = adapter.classifyTarget(state, markerContract, msg, execution::FrameScope::Nested);
    BOOST_REQUIRE(desc.has_value());
    auto result = adapter.dispatch(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(called);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(static_warm_enumerate_is_empty)
{
    state::test::InMemoryStateView base;
    state::State state{base};
    InMemoryChainPrecompileAdapter dispatchPort;
    FiscoChainCallTargetAdapter adapter(state, dispatchPort);

    size_t count = 0;
    adapter.forEachStaticWarmTarget([&](evmc_address const&) { ++count; });
    BOOST_CHECK_EQUAL(count, 0U);

    evmc_message msg{};
    msg.recipient = fiscoPrecompileAddress(0x1003);
    msg.code_address = msg.recipient;
    auto desc = adapter.classifyTarget(state, msg.recipient, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(desc.has_value());
    BOOST_CHECK(desc->warmPolicy == execution::WarmPolicy::Never);
}

}  // namespace bcos::evm::test
