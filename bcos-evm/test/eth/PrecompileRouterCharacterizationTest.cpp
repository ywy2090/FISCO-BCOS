/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief PR-1 characterization baseline: depth=0 vs depth=1 precompile dispatch (C1–C7).
 * @file PrecompileRouterCharacterizationTest.cpp
 */
#define BOOST_TEST_MODULE PrecompileRouterCharacterizationTest

#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpHostExtension.h"
#include "bcos-evm/opstack/OpStackConstants.h"
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
// BASELINE(pre-router): C1 identity 0x04 — depth=0 via executeMessage
constexpr evmc_status_code kC1Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC1Depth0GasLeft = 499'982;

// BASELINE(pre-router): C1 identity 0x04 — depth=1 via EthHost::call
constexpr evmc_status_code kC1Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC1Depth1GasLeft = 499'982;

// BASELINE(pre-router): C2 Op L1Block chain hook — depth=0
constexpr evmc_status_code kC2Depth0Status = EVMC_REVERT;
constexpr int64_t kC2Depth0GasLeft = 300'000;

// BASELINE(pre-router): C2 Op L1Block chain hook — depth=1
constexpr evmc_status_code kC2Depth1Status = EVMC_REVERT;
constexpr int64_t kC2Depth1GasLeft = 300'000;

// BASELINE(pre-router): C3 empty EOA — depth=0
constexpr evmc_status_code kC3Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC3Depth0GasLeft = 49'940;

// BASELINE(pre-router): C3 empty EOA — depth=1
constexpr evmc_status_code kC3Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC3Depth1GasLeft = 49'940;

// BASELINE(pre-router): C4 DELEGATECALL → precompile with allowDelegateCallToPrecompile=false
constexpr evmc_status_code kC4Depth1Status = EVMC_PRECOMPILE_FAILURE;
constexpr int64_t kC4Depth1GasLeft = 100'000;

// BASELINE(pre-router): C5 CALL + value → identity 0x04 — depth=0
constexpr evmc_status_code kC5Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC5Depth0GasLeft = 499'982;
constexpr uint64_t kC5Depth0RecipientBalance = 100;

// BASELINE(router): C5 CALL + value → identity 0x04 — depth=1
constexpr evmc_status_code kC5Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC5Depth1GasLeft = 499'982;
constexpr uint64_t kC5Depth1RecipientBalance = 100;

// BASELINE(pre-router): C6 BLS 0x0b at CANCUN (inactive) — depth=0
constexpr evmc_status_code kC6Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC6Depth0GasLeft = 100'000;

// BASELINE(pre-router): C6 BLS 0x0b at CANCUN (inactive) — depth=1
constexpr evmc_status_code kC6Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC6Depth1GasLeft = 100'000;

// BASELINE(pre-router): C7 [PRECOMPILED] non-empty code asymmetry — depth=0 (vm.execute)
constexpr evmc_status_code kC7Depth0Status = EVMC_STACK_UNDERFLOW;

// BASELINE(pre-router): C7 [PRECOMPILED] non-empty code asymmetry — depth=1 (chain hook)
constexpr evmc_status_code kC7Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC7Depth1GasLeft = 50'000;

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

evmc_address precompileAddress(uint8_t lowByte, uint8_t highByte = 0x00)
{
    evmc_address addr{};
    addr.bytes[18] = highByte;
    addr.bytes[19] = lowByte;
    return addr;
}

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}

evmc_uint256be oneWeiValue()
{
    evmc_uint256be value{};
    value.bytes[31] = 100;
    return value;
}

bytes setterSelector()
{
    return {0x09, 0x89, 0x99, 0xbe};
}

CallOutcome runDepth0EmptyCall(ExecuteMessageInput input)
{
    auto const recipient = std::memcmp(input.message.code_address.bytes, evmc_address{}.bytes,
                               sizeof(evmc_address{}.bytes)) != 0 ?
                               input.message.code_address :
                               input.message.recipient;
    auto* stateView = input.stateView;
    auto output = executeMessage(std::move(input));
    CallOutcome outcome;
    outcome.status = output.result.status_code;
    outcome.gasLeft = output.result.gas_left;
    if (auto* statePtr = dynamic_cast<state::State const*>(stateView); statePtr != nullptr)
    {
        outcome.recipientBalance = statePtr->get_balance(recipient);
    }
    else if (auto* viewPtr = dynamic_cast<state::test::InMemoryStateView const*>(stateView);
             viewPtr != nullptr)
    {
        if (auto acct = viewPtr->get_account(recipient))
        {
            outcome.recipientBalance = acct->balance;
        }
    }
    return outcome;
}

CallOutcome runDepth1EmptyCall(state::State& state, state::EthHost& host, evmc_message msg)
{
    auto result = host.call(msg);
    CallOutcome outcome;
    outcome.status = result.status_code;
    outcome.gasLeft = result.gas_left;
    auto const recipient = std::memcmp(msg.code_address.bytes, evmc_address{}.bytes,
                               sizeof(evmc_address{}.bytes)) != 0 ?
                               msg.code_address :
                               msg.recipient;
    outcome.recipientBalance = state.get_balance(recipient);
    return outcome;
}

struct Depth1HostFixture
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    bcos::evm_standard::RevisionConfig cfg{};
    std::optional<state::EthHost> host;

    Depth1HostFixture(
        state::State& state, state::HostExtension* extension, evmc_revision revision = EVMC_PRAGUE)
    {
        txContext.block_gas_limit = 30'000'000;
        cfg = {.revision = revision, .warm_access = true};
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), extension, false);
    }

    state::EthHost& ethHost() { return *host; }
};

ExecuteMessageInput makeBaseInput(
    state::StateView* view, evmc_message const& message, state::HostExtension* extension = nullptr)
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
    input.extension = extension;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(c1_identity_precompile_depth0_and_depth1)
{
    // C1: builtin identity 0x04, empty code, input 0xdeadbeef
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    state::test::InMemoryStateView view;
    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = identity;
    message.code_address = identity;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    auto depth0 = runDepth0EmptyCall(makeBaseInput(&view, message));
    BOOST_CHECK_EQUAL(depth0.status, kC1Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC1Depth0GasLeft);

    state::test::InMemoryStateView view1;
    state::State state(view1);
    state.set_balance(sender, senderAccount.balance);
    Depth1HostFixture fixture(state, nullptr);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC1Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC1Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c2_op_l1block_chain_hook_depth0_and_depth1)
{
    // C2: Op L1Block chain hook — reference EmptyCodeHookTest pattern
    auto calldata = setterSelector();

    state::test::InMemoryStateView baseState;
    state::State state0(baseState);
    OpHostExtension extension0(&state0);
    state0.set_balance(OP_DEPOSITOR_ACCOUNT, 1'000'000);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 300'000;
    message.sender = OP_DEPOSITOR_ACCOUNT;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    auto depth0 = runDepth0EmptyCall(makeBaseInput(&state0, message, &extension0));
    BOOST_CHECK_EQUAL(depth0.status, kC2Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC2Depth0GasLeft);

    state::test::InMemoryStateView baseState1;
    state::State state1(baseState1);
    OpHostExtension extension1(&state1);
    state1.set_balance(OP_DEPOSITOR_ACCOUNT, 1'000'000);
    Depth1HostFixture fixture(state1, &extension1);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state1, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC2Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC2Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c3_empty_eoa_depth0_and_depth1)
{
    // C3: empty EOA call — reference ExecuteMessageSmokeTest pattern
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    state::test::InMemoryStateView view;
    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    auto depth0 = runDepth0EmptyCall(makeBaseInput(&view, message));
    BOOST_CHECK_EQUAL(depth0.status, kC3Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC3Depth0GasLeft);

    state::test::InMemoryStateView view1;
    state::State state(view1);
    state.set_balance(sender, senderAccount.balance);
    Depth1HostFixture fixture(state, nullptr);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC3Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC3Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c4_delegatecall_to_precompile_blocked_at_depth1)
{
    // C4: DELEGATECALL → precompile with allowDelegateCallToPrecompile=false (FiscoHostExtension)
    auto const caller = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(caller, 1'000'000);
    FiscoHostExtension extension(/*skipEvmNativeValueTransfer*/ true);
    Depth1HostFixture fixture(state, &extension);

    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.depth = 1;
    msg.gas = 100'000;
    msg.sender = caller;
    msg.recipient = caller;
    msg.code_address = identity;

    auto depth1 = runDepth1EmptyCall(state, fixture.ethHost(), msg);
    BOOST_CHECK_EQUAL(depth1.status, kC4Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC4Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c5_call_with_value_to_identity_depth0_and_depth1)
{
    // C5: CALL + non-zero value → builtin identity 0x04
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    auto const value = oneWeiValue();
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};

    state::test::InMemoryStateView baseState;
    state::State state0(baseState);
    state0.set_balance(sender, 1'000'000);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = identity;
    message.code_address = identity;
    message.value = value;
    message.input_data = inputBytes.data();
    message.input_size = inputBytes.size();

    auto depth0 = runDepth0EmptyCall(makeBaseInput(&state0, message));
    BOOST_CHECK_EQUAL(depth0.status, kC5Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC5Depth0GasLeft);
    BOOST_CHECK_EQUAL(depth0.recipientBalance, kC5Depth0RecipientBalance);

    state::test::InMemoryStateView view1;
    state::State state(view1);
    state.set_balance(sender, 1'000'000);
    Depth1HostFixture fixture(state, nullptr);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC5Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC5Depth1GasLeft);
    BOOST_CHECK_EQUAL(depth1.recipientBalance, kC5Depth1RecipientBalance);
}

BOOST_AUTO_TEST_CASE(c6_revision_gate_bls_inactive_at_cancun)
{
    // C6: BLS precompile 0x0b inactive at CANCUN — revision gate baseline
    auto const sender = addressFromLastByte(0x01);
    auto const bls = precompileAddress(0x0b);
    BOOST_CHECK(!precompiled::isActivePrecompile(EVMC_CANCUN, {}, bls));

    state::test::InMemoryStateView view;
    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    message.sender = sender;
    message.recipient = bls;
    message.code_address = bls;

    auto input = makeBaseInput(&view, message);
    input.revisionConfig.revision = EVMC_CANCUN;
    auto depth0 = runDepth0EmptyCall(std::move(input));
    BOOST_CHECK_EQUAL(depth0.status, kC6Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC6Depth0GasLeft);

    state::test::InMemoryStateView view1;
    state::State state(view1);
    state.set_balance(sender, senderAccount.balance);
    Depth1HostFixture fixture(state, nullptr, EVMC_CANCUN);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC6Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC6Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c7_precompiled_marker_asymmetry_depth0_vs_depth1)
{
    // C7: non-empty [PRECOMPILED] code — depth=0 runs vm.execute, depth=1 hits chain hook.
    // PR-2 does NOT require equivalence for this scenario.
    auto const sender = addressFromLastByte(0x01);
    auto const markerContract = addressFromLastByte(0x22);
    evmc_address expectedTarget{};
    expectedTarget.bytes[18] = 0x10;
    expectedTarget.bytes[19] = 0x03;

    auto markerCode = std::string("[PRECOMPILED],0000000000000000000000000000000000001003");

    bool callbackInvoked = false;
    auto callback = [&callbackInvoked, expectedTarget](evmc_revision /*rev*/,
                        const evmc_message& message) -> std::optional<evmc_result> {
        callbackInvoked = true;
        BOOST_CHECK_EQUAL(std::memcmp(message.code_address.bytes, expectedTarget.bytes,
                              sizeof(expectedTarget.bytes)),
            0);
        evmc_result result{};
        result.status_code = EVMC_SUCCESS;
        result.gas_left = message.gas;
        return result;
    };

    state::test::InMemoryStateView view;
    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);
    state::Account markerAccount;
    markerAccount.code = bcos::bytes(markerCode.begin(), markerCode.end());
    view.insert_account(markerContract, markerAccount);

    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = nullptr;
    FiscoHostExtension extension(/*skipEvmNativeValueTransfer*/ true, std::move(deps), callback);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = markerContract;
    message.code_address = markerContract;

    CallOutcome depth0Outcome;
    // depth=0: non-empty code bypasses chain hook → EVM executes marker bytecode
    {
        state::test::InMemoryStateView depth0View;
        depth0View.insert_account(sender, senderAccount);
        depth0View.insert_account(markerContract, markerAccount);
        FiscoHostExtension::FiscoHostExtensionDeps depth0Deps;
        state::State depth0State(depth0View);
        depth0Deps.state = &depth0State;
        FiscoHostExtension depth0Ext(/*skipEvmNativeValueTransfer*/ true, std::move(depth0Deps),
            [&callbackInvoked](evmc_revision /*rev*/, const evmc_message& /*msg*/) {
                callbackInvoked = true;
                evmc_result result{};
                result.status_code = EVMC_SUCCESS;
                return std::optional<evmc_result>{result};
            });
        depth0Outcome = runDepth0EmptyCall(makeBaseInput(&depth0State, message, &depth0Ext));
        BOOST_CHECK(!callbackInvoked);
        BOOST_CHECK_EQUAL(depth0Outcome.status, kC7Depth0Status);
        BOOST_CHECK(depth0Outcome.status != kC7Depth1Status);
    }

    // depth=1: chain hook runs before EVM regardless of non-empty code
    callbackInvoked = false;
    state::test::InMemoryStateView view1;
    state::State state(view1);
    state.set_balance(sender, senderAccount.balance);
    state.set_code(markerContract, markerAccount.code, {});
    FiscoHostExtension::FiscoHostExtensionDeps depth1Deps;
    depth1Deps.state = &state;
    FiscoHostExtension depth1Ext(
        /*skipEvmNativeValueTransfer*/ true, std::move(depth1Deps), callback);
    Depth1HostFixture fixture(state, &depth1Ext);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state, fixture.ethHost(), depth1Msg);
    BOOST_CHECK(callbackInvoked);
    BOOST_CHECK_EQUAL(depth1.status, kC7Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC7Depth1GasLeft);
    BOOST_CHECK(depth0Outcome.status != depth1.status);
}

}  // namespace bcos::evm::test
