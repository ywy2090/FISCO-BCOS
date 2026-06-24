/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile router value-transfer envelope tests.
 */

#define BOOST_TEST_MODULE PrecompileRouterEnvelopeTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstring>
#include <optional>

namespace bcos::evm::test
{
namespace
{
struct CallOutcome
{
    evmc_status_code status{};
    int64_t gasLeft{};
    bcos::u256 senderBalance{};
    bcos::u256 recipientBalance{};
};

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_address precompileAddress(uint8_t lowByte)
{
    evmc_address addr{};
    addr.bytes[19] = lowByte;
    return addr;
}

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}

evmc_uint256be weiValue(uint8_t value)
{
    evmc_uint256be out{};
    out.bytes[31] = value;
    return out;
}

evmc_address balanceTarget(evmc_message const& msg)
{
    return std::memcmp(
               msg.code_address.bytes, evmc_address{}.bytes, sizeof(evmc_address{}.bytes)) != 0 ?
               msg.code_address :
               msg.recipient;
}

ExecuteMessageInput makeBaseInput(state::StateView* view, evmc_message const& message)
{
    static evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.stateView = view;
    input.vm = &vm;
    input.message = message;
    input.blockInfo.number = 1;
    input.blockInfo.gasLimit = 30'000'000;
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.warm_access = true;
    input.txProps.warmDestination = true;
    return input;
}

CallOutcome runDepth0(state::State& state, evmc_message const& message)
{
    auto output = executeMessage(makeBaseInput(&state, message));
    return {.status = output.result.status_code,
        .gasLeft = output.result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

struct Depth1HostFixture
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm_standard::RevisionConfig cfg{};
    std::optional<state::EthHost> host;

    explicit Depth1HostFixture(state::State& state)
    {
        txContext.block_gas_limit = 30'000'000;
        cfg = {.revision = EVMC_PRAGUE, .warm_access = true};
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, false);
    }

    state::EthHost& ethHost() { return *host; }
};

CallOutcome runDepth1(state::State& state, evmc_message message)
{
    Depth1HostFixture fixture(state);
    message.depth = 1;
    auto result = fixture.ethHost().call(message);
    return {.status = result.status_code,
        .gasLeft = result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

evmc_message valueTransferMessage(evmc_address sender, evmc_address recipient, evmc_uint256be value,
    std::array<uint8_t, 4> const& inputBytes)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;
    message.value = value;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();
    return message;
}
}  // namespace

BOOST_AUTO_TEST_CASE(c5_insufficient_balance_both_depths)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    state0.set_balance(sender, 99);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, 99);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, 0);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, 0);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
}

BOOST_AUTO_TEST_CASE(successful_value_transfer_balances_match_depth0_and_depth1)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    state0.set_balance(sender, 1'000'000);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
}

}  // namespace bcos::evm::test
