/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile router depth equivalence tests.
 */

#define BOOST_TEST_MODULE PrecompileRouterEquivalenceTest

#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/host/EthHost.hpp"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "helpers/InMemoryStateView.h"
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

bytes setterSelector()
{
    return {0x09, 0x89, 0x99, 0xbe};
}

evmc_address balanceTarget(evmc_message const& msg)
{
    return std::memcmp(
               msg.code_address.bytes, evmc_address{}.bytes, sizeof(evmc_address{}.bytes)) != 0 ?
               msg.code_address :
               msg.recipient;
}

InnerExecuteInput makeBaseInput(state::State& state, evmc_message const& message,
    ChainCallTargetDispatcher* chainPort = nullptr)
{
    static evmc::VM vm{evmc_create_evmone()};
    InnerExecuteInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo.number = 1;
    input.blockInfo.gasLimit = 30'000'000;
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.eip2929 = true;
    input.txProps.warmDestination = true;
    input.chainPort = chainPort;
    return input;
}

CallOutcome runDepth0(state::State& state, evmc_message const& message,
    ChainCallTargetDispatcher* chainPort = nullptr)
{
    auto output = innerExecute(makeBaseInput(state, message, chainPort));
    return {.status = output.result.status_code,
        .gasLeft = output.result.gas_left,
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

struct Depth1HostFixture
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm_standard::RevisionConfig cfg{};
    std::optional<state::EthHost> host;

    Depth1HostFixture(state::State& state, ChainCallTargetDispatcher* chainPort = nullptr)
    {
        txContext.block_gas_limit = 30'000'000;
        cfg = {.revision = EVMC_PRAGUE, .eip2929 = true};
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, false, chainPort);
    }

    state::EthHost& ethHost() { return *host; }
};

CallOutcome runDepth1(
    state::State& state, evmc_message message, ChainCallTargetDispatcher* chainPort = nullptr)
{
    Depth1HostFixture fixture(state, chainPort);
    message.depth = 1;
    auto result = fixture.ethHost().call(message);
    return {.status = result.status_code,
        .gasLeft = result.gas_left,
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

void requireEquivalent(CallOutcome const& depth0, CallOutcome const& depth1)
{
    BOOST_REQUIRE_EQUAL(depth0.status, depth1.status);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, depth1.gasLeft);
}
}  // namespace

BOOST_AUTO_TEST_CASE(c1_identity_depth0_equals_depth1)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = identity;
    message.code_address = identity;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    state0.set_balance(sender, 1'000'000);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state1, message);

    requireEquivalent(depth0, depth1);
}

BOOST_AUTO_TEST_CASE(c2_chain_hook_depth0_equals_depth1)
{
    auto calldata = setterSelector();

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 300'000;
    message.sender = OP_DEPOSITOR_ACCOUNT;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    OpStackChainCallTargetAdapter chainAdapter0(&state0, 0, makeIsthmusPlusForkSchedule(), 0);
    state0.set_balance(OP_DEPOSITOR_ACCOUNT, 1'000'000);
    auto depth0 = runDepth0(state0, message, &chainAdapter0);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    OpStackChainCallTargetAdapter chainAdapter1(&state1, 0, makeIsthmusPlusForkSchedule(), 0);
    state1.set_balance(OP_DEPOSITOR_ACCOUNT, 1'000'000);
    auto depth1 = runDepth1(state1, message, &chainAdapter1);

    requireEquivalent(depth0, depth1);
}

BOOST_AUTO_TEST_CASE(c3_empty_eoa_depth0_equals_depth1)
{
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    state0.set_balance(sender, 1'000'000);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state1, message);

    requireEquivalent(depth0, depth1);
}

BOOST_AUTO_TEST_CASE(c5_value_transfer_depth0_equals_depth1)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    auto const value = weiValue(100);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = identity;
    message.code_address = identity;
    message.value = value;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    state0.set_balance(sender, 1'000'000);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state1, message);

    requireEquivalent(depth0, depth1);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
}

}  // namespace bcos::evm::test
