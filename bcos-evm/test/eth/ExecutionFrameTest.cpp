/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief ExecutionFrame PR1 gate tests — nested parity + TopLevel characterization.
 */

#define BOOST_TEST_MODULE ExecutionFrameTest

#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>
#include <array>
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

BOOST_AUTO_TEST_CASE(top_level_frame_does_not_commit_before_adapter_nonce_bump)
{
    auto const sender = addressFromLastByte(0x10);
    auto const target = addressFromLastByte(0x20);
    bcos::bytes stopCode{0x00};

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    state.set_nonce(sender, 5);
    state.set_code(target, stopCode,
        state::keccak256Code(bcos::bytesConstRef{stopCode.data(), stopCode.size()}));

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = nullptr;
    message.input_size = 0;

    FrameTestHost fixture(state);
    execution::FrameContext frameCtx{state, fixture.vm, fixture.cfg, nullptr,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};
    auto fr = execution::runExecutionFrame(
        frameCtx, message, execution::FrameScope::TopLevel, fixture.ethHost());

    BOOST_REQUIRE_EQUAL(fr.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(state.get_nonce(sender), 5U);
    BOOST_REQUIRE(state.has_checkpoint());

    state.set_nonce(sender, 6);
    state.commit();
    BOOST_REQUIRE(!state.has_checkpoint());

    auto const diff = state.build_diff();
    auto const senderIt = diff.accounts.find(sender);
    BOOST_REQUIRE(senderIt != diff.accounts.end());
    BOOST_CHECK_EQUAL(senderIt->second.nonce, 6U);
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

BOOST_AUTO_TEST_CASE(nested_create_insufficient_balance_characterization)
{
    auto const sender = addressFromLastByte(0x01);
    auto const createAddr = addressFromLastByte(0x42);

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.depth = 1;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = createAddr;
    message.value = weiValue(100);
    message.input_data = nullptr;
    message.input_size = 0;

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 50);

    auto frame = runFrameNested(state, message);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, 0);
}

BOOST_AUTO_TEST_CASE(top_level_precompile_insufficient_balance_matches_envelope_test)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryEvmStateReader view0;
    state::State state0(view0);
    state0.set_balance(sender, 99);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryEvmStateReader view1;
    state::State state1(view1);
    state1.set_balance(sender, 99);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, depth1.status);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, 0);
}

BOOST_AUTO_TEST_CASE(top_level_successful_value_transfer_matches_envelope_test)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryEvmStateReader view0;
    state::State state0(view0);
    state0.set_balance(sender, 1'000'000);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryEvmStateReader view1;
    state::State state1(view1);
    state1.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, depth1.status);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(top_level_sender_nonce_bump_on_success)
{
    auto const sender = addressFromLastByte(0x10);
    auto const target = addressFromLastByte(0x20);
    bcos::bytes stopCode{0x00};

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    state.set_nonce(sender, 5);
    state.set_code(target, stopCode,
        state::keccak256Code(bcos::bytesConstRef{stopCode.data(), stopCode.size()}));

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = nullptr;
    message.input_size = 0;

    auto const output = executeMessage(makeBaseInput(&state, message));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(state.get_nonce(sender), 6U);
}

BOOST_AUTO_TEST_CASE(top_level_precompile_hit_skips_finalize_self_destructs)
{
    auto const sender = addressFromLastByte(0x01);
    auto const victim = addressFromLastByte(0x99);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(0), inputBytes);

    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    state.set_balance(victim, 500);
    state.set_code(victim, bcos::bytes{0x60, 0x00}, {});
    state.mark_self_destructed(victim);

    auto const output = executeMessage(makeBaseInput(&state, message));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE(state.has_self_destructed(victim));
    BOOST_REQUIRE_EQUAL(state.get_balance(victim), 500U);
    BOOST_REQUIRE(!state.get_code(victim).empty());

    auto const victimIt = output.stateDiff.accounts.find(victim);
    BOOST_REQUIRE(victimIt != output.stateDiff.accounts.end());
    BOOST_CHECK(victimIt->second.selfDestructed);
    BOOST_CHECK_GT(victimIt->second.balance, 0U);
}

}  // namespace bcos::evm::test
