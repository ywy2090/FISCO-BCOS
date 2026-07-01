/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief ExecutionFrame PR1 gate tests — nested parity + TopLevel characterization.
 */

#define BOOST_TEST_MODULE EvmCallFrameTest

#include "bcos-evm/eth/kernel/execution/EvmCallFrame.h"
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/kernel/execution/CreateContract.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryStateView.h"
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

struct DenyDelegatePrecompilePolicy : state::EvmHostHooks
{
    bool allowDelegateCallToPrecompile() override { return false; }
};

struct FrameTestHost
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2929 = true};
    std::optional<state::EthHost> host;

    explicit FrameTestHost(state::State& state, state::EvmHostHooks* extension = nullptr)
    {
        txContext.block_gas_limit = 30'000'000;
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), extension);
    }

    state::EthHost& ethHost() { return *host; }
};

CallOutcome runFrameNested(
    state::State& state, evmc_message message, state::EvmHostHooks* extension = nullptr)
{
    FrameTestHost fixture(state, extension);
    message.depth = 1;
    execution::FrameExecutionEnv frameCtx{state, fixture.vm, fixture.cfg, extension,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};
    auto fr = execution::runCallFrame(
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
    execution::FrameExecutionEnv frameCtx{state, fixture.vm, fixture.cfg, nullptr,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};
    auto fr = execution::runCallFrame(
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

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 99);

    auto depth1 = runDepth1(state, message);

    state::test::InMemoryStateView viewFrame;
    state::State stateFrame(viewFrame);
    stateFrame.set_balance(sender, 99);
    auto frame = runFrameNested(stateFrame, message);

    BOOST_REQUIRE_EQUAL(frame.status, depth1.status);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(frame.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(frame.recipientBalance, depth1.recipientBalance);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, 500'000);
}

BOOST_AUTO_TEST_CASE(nested_successful_value_transfer_matches_envelope_test)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state, message);

    state::test::InMemoryStateView viewFrame;
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
    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto frame = runFrameNested(state, message, &policy);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE(!frame.precompileHit);
}

BOOST_AUTO_TEST_CASE(nested_7702_delegatecall_direct_precompile_hits_envelope)
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

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    auto frame = runFrameNested(state, message);
    BOOST_REQUIRE(frame.precompileHit);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, 500'000 - 18);
}

BOOST_AUTO_TEST_CASE(top_level_precompile_hit_sets_precompileHit)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(0), inputBytes);

    state::test::InMemoryStateView view;
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

    state::test::InMemoryStateView view;
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
    execution::FrameExecutionEnv frameCtx{state, fixture.vm, fixture.cfg, nullptr,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};
    auto fr = execution::runCallFrame(
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

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 50);

    auto frame = runFrameTopLevel(state, message);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, 500'000);
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

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 50);

    auto frame = runFrameNested(state, message);
    BOOST_REQUIRE_EQUAL(frame.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(frame.gasLeft, 500'000);
}

BOOST_AUTO_TEST_CASE(top_level_precompile_insufficient_balance_matches_envelope_test)
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

    BOOST_REQUIRE_EQUAL(depth0.status, depth1.status);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, 500'000);
}

BOOST_AUTO_TEST_CASE(top_level_successful_value_transfer_matches_envelope_test)
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

    state::test::InMemoryStateView view;
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

    auto const output = innerExecute(makeBaseInput(&state, message));
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

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    state.set_balance(victim, 500);
    state.set_code(victim, bcos::bytes{0x60, 0x00}, {});
    state.mark_self_destructed(victim);

    auto const output = innerExecute(makeBaseInput(&state, message));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE(state.has_self_destructed(victim));
    BOOST_REQUIRE_EQUAL(state.get_balance(victim), 500U);
    BOOST_REQUIRE(!state.get_code(victim).empty());

    auto const victimIt = output.stateDiff.accounts.find(victim);
    BOOST_REQUIRE(victimIt != output.stateDiff.accounts.end());
    BOOST_CHECK(victimIt->second.selfDestructed);
    BOOST_CHECK_GT(victimIt->second.balance, 0U);
}

BOOST_AUTO_TEST_CASE(nested_create_failed_still_increments_sender_nonce)
{
    auto const sender = addressFromLastByte(0x01);
    static uint8_t invalidInit[] = {0xfe};

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.depth = 1;
    message.gas = 500'000;
    message.sender = sender;
    message.input_data = invalidInit;
    message.input_size = sizeof(invalidInit);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    state.set_nonce(sender, 7);

    auto frame = runFrameNested(state, message);
    BOOST_REQUIRE(frame.status != EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_nonce(sender), 8U);
}

BOOST_AUTO_TEST_CASE(nested_create_sequential_assigns_distinct_addresses)
{
    auto const sender = addressFromLastByte(0x01);
    auto const firstChild = state::predictLegacyCreateAddress(sender, 7);
    auto const secondChild = state::predictLegacyCreateAddress(sender, 8);
    static uint8_t emptyInit[] = {0x60, 0x00, 0x60, 0x00, 0xf3};

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);
    state.set_nonce(sender, 7);

    FrameTestHost fixture(state);
    execution::FrameExecutionEnv frameCtx{state, fixture.vm, fixture.cfg, nullptr,
        fixture.txContext.tx_origin, fixture.ethHost().execution_address_ref()};

    evmc_message create1{};
    create1.kind = EVMC_CREATE;
    create1.depth = 1;
    create1.gas = 500'000;
    create1.sender = sender;
    create1.input_data = emptyInit;
    create1.input_size = sizeof(emptyInit);

    auto fr1 = execution::runCallFrame(
        frameCtx, create1, execution::FrameScope::Nested, fixture.ethHost());
    BOOST_REQUIRE_EQUAL(fr1.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(state.get_nonce(firstChild), 1U);

    evmc_message create2 = create1;
    create2.recipient = {};
    create2.code_address = {};
    auto fr2 = execution::runCallFrame(
        frameCtx, create2, execution::FrameScope::Nested, fixture.ethHost());
    BOOST_REQUIRE_EQUAL(fr2.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(state.get_nonce(secondChild), 1U);
    BOOST_CHECK(std::memcmp(firstChild.bytes, secondChild.bytes, sizeof(firstChild.bytes)) != 0);
    BOOST_CHECK_EQUAL(state.get_nonce(sender), 9U);
}

BOOST_AUTO_TEST_CASE(nested_create_reentrant_address_derivation_sees_pre_checkpoint_bump)
{
    auto const deployer = addressFromLastByte(0x01);
    auto const firstChild = state::predictLegacyCreateAddress(deployer, 7);
    auto const secondChild = state::predictLegacyCreateAddress(deployer, 8);
    static uint8_t emptyInit[] = {0x60, 0x00, 0x60, 0x00, 0xf3};

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_nonce(deployer, 7);

    FrameTestHost fixture(state);
    auto& host = fixture.ethHost();

    evmc_message outer{};
    outer.kind = EVMC_CREATE;
    outer.sender = deployer;
    outer.input_data = emptyInit;
    outer.input_size = sizeof(emptyInit);

    execution::bindCreateMessageForInit(
        host, outer, bcos::bytesConstRef(outer.input_data, outer.input_size), state);
    BOOST_REQUIRE(
        std::memcmp(outer.recipient.bytes, firstChild.bytes, sizeof(firstChild.bytes)) == 0);

    state.set_nonce(deployer, state.get_nonce(deployer) + 1);

    evmc_message inner = outer;
    inner.recipient = {};
    inner.code_address = {};
    execution::bindCreateMessageForInit(
        host, inner, bcos::bytesConstRef(inner.input_data, inner.input_size), state);
    BOOST_REQUIRE(
        std::memcmp(inner.recipient.bytes, secondChild.bytes, sizeof(secondChild.bytes)) == 0);
}

}  // namespace bcos::evm::test
