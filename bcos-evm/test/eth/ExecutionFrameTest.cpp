/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief ExecutionFrame PR1 gate tests — nested parity + TopLevel characterization.
 */

#define BOOST_TEST_MODULE ExecutionFrameTest

#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "state/InMemoryEvmStateReader.h"
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
    bool precompileHit{false};
};

struct DenyDelegatePrecompilePolicy : state::VmHostPolicy
{
    bool allowDelegateCallToPrecompile() override { return false; }
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

struct FrameTestHost
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .warm_access = true};
    std::optional<state::EthHost> host;

    explicit FrameTestHost(state::State& state, state::VmHostPolicy* extension = nullptr)
    {
        txContext.block_gas_limit = 30'000'000;
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), extension, false);
    }

    state::EthHost& ethHost() { return *host; }
};

CallOutcome runDepth1(state::State& state, evmc_message message)
{
    FrameTestHost fixture(state);
    message.depth = 1;
    auto result = fixture.ethHost().call(message);
    return {.status = result.status_code,
        .gasLeft = result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message))};
}

CallOutcome runFrameNested(
    state::State& state, evmc_message message, state::VmHostPolicy* extension = nullptr)
{
    FrameTestHost fixture(state, extension);
    message.depth = 1;
    execution::FrameContext frameCtx{state, fixture.vm, fixture.cfg, extension,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};
    auto fr = execution::runExecutionFrame(
        frameCtx, message, execution::FrameScope::Nested, fixture.ethHost());
    return {.status = fr.result.status_code,
        .gasLeft = fr.result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message)),
        .precompileHit = fr.precompileHit};
}

CallOutcome runFrameTopLevel(state::State& state, evmc_message message)
{
    FrameTestHost fixture(state);
    execution::FrameContext frameCtx{state, fixture.vm, fixture.cfg, nullptr,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};
    auto fr = execution::runExecutionFrame(
        frameCtx, message, execution::FrameScope::TopLevel, fixture.ethHost());
    return {.status = fr.result.status_code,
        .gasLeft = fr.result.gas_left,
        .senderBalance = state.get_balance(message.sender),
        .recipientBalance = state.get_balance(balanceTarget(message)),
        .precompileHit = fr.precompileHit};
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

BOOST_AUTO_TEST_CASE(nested_precompile_insufficient_balance_matches_envelope_test)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 99);

    auto depth1 = runDepth1(state, message);

    state::test::InMemoryEvmStateReader viewFrame;
    state::State stateFrame(viewFrame);
    stateFrame.set_balance(sender, 99);
    auto frame = runFrameNested(stateFrame, message);

    BOOST_REQUIRE_EQUAL(frame.status, depth1.status);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(frame.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(frame.recipientBalance, depth1.recipientBalance);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, 0);
}

BOOST_AUTO_TEST_CASE(nested_successful_value_transfer_matches_envelope_test)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state, message);

    state::test::InMemoryEvmStateReader viewFrame;
    state::State stateFrame(viewFrame);
    stateFrame.set_balance(sender, 1'000'000);
    auto frame = runFrameNested(stateFrame, message);

    BOOST_REQUIRE_EQUAL(frame.status, depth1.status);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(frame.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(frame.recipientBalance, depth1.recipientBalance);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(nested_delegatecall_precompile_blocked)
{
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.depth = 1;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = identity;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    DenyDelegatePrecompilePolicy policy;
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto frame = runFrameNested(state, message, &policy);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE(!frame.precompileHit);
}

BOOST_AUTO_TEST_CASE(nested_7702_delegatecall_precompile_guard)
{
    auto const sender = addressFromLastByte(0x01);
    auto const caller = addressFromLastByte(0x02);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    evmc_message message{};
    message.kind = EVMC_DELEGATECALL;
    message.flags = EVMC_DELEGATED;
    message.depth = 1;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = identity;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto frame = runFrameNested(state, message);
    BOOST_REQUIRE(!frame.precompileHit);
    BOOST_REQUIRE(frame.status != EVMC_PRECOMPILE_FAILURE);
}

BOOST_AUTO_TEST_CASE(top_level_precompile_hit_sets_precompileHit)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(0), inputBytes);

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto frame = runFrameTopLevel(state, message);
    BOOST_REQUIRE(frame.precompileHit);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(top_level_create_checkpoint_before_bind_order)
{
    auto const sender = addressFromLastByte(0x01);
    auto const createAddr = addressFromLastByte(0x42);

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = createAddr;
    message.value = weiValue(100);
    message.input_data = nullptr;
    message.input_size = 0;

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 50);

    auto frame = runFrameTopLevel(state, message);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, 0);
}

}  // namespace bcos::evm::test
