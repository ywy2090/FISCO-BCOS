/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief characterization baseline: depth=0 vs depth=1 call-target dispatch (C1–C7).
 * @file CallTargetCharacterizationTest.cpp
 */
#define BOOST_TEST_MODULE CallTargetCharacterizationTest

#include "bcos-evm/bcos/FiscoChainCallTargetAdapter.h"
#include "bcos-evm/bcos/FiscoVmHostPolicy.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/host/EthHost.hpp"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos/adapters/InMemoryChainCallTargetAdapter.h"
#include "bcos/adapters/InMemoryChainPrecompileAdapter.h"
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
// BASELINE(call-target-resolver): C1 identity 0x04 — depth=0 via innerExecute
constexpr evmc_status_code kC1Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC1Depth0GasLeft = 499'982;

// BASELINE(call-target-resolver): C1 identity 0x04 — depth=1 via EthHost::call
constexpr evmc_status_code kC1Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC1Depth1GasLeft = 499'982;

// BASELINE(call-target-resolver): C2 Op L1Block chain hook — depth=0
constexpr evmc_status_code kC2Depth0Status = EVMC_REVERT;
constexpr int64_t kC2Depth0GasLeft = 300'000;

// BASELINE(call-target-resolver): C2 Op L1Block chain hook — depth=1
constexpr evmc_status_code kC2Depth1Status = EVMC_REVERT;
constexpr int64_t kC2Depth1GasLeft = 300'000;

// BASELINE(call-target-resolver): C3 empty EOA — depth=0
constexpr evmc_status_code kC3Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC3Depth0GasLeft = 49'940;

// BASELINE(call-target-resolver): C3 empty EOA — depth=1
constexpr evmc_status_code kC3Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC3Depth1GasLeft = 49'940;

// BASELINE(call-target-resolver): C4 DELEGATECALL → precompile with
// allowDelegateCallToPrecompile=false
constexpr evmc_status_code kC4Depth1Status = EVMC_PRECOMPILE_FAILURE;
constexpr int64_t kC4Depth1GasLeft = 100'000;

// BASELINE(call-target-resolver): C5 CALL + value → identity 0x04 — depth=0
constexpr evmc_status_code kC5Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC5Depth0GasLeft = 499'982;
constexpr uint64_t kC5Depth0RecipientBalance = 100;

// BASELINE(call-target-resolver): C5 CALL + value → identity 0x04 — depth=1
constexpr evmc_status_code kC5Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC5Depth1GasLeft = 499'982;
constexpr uint64_t kC5Depth1RecipientBalance = 100;

// BASELINE(call-target-resolver): C6 BLS 0x0b at CANCUN (inactive) — depth=0
constexpr evmc_status_code kC6Depth0Status = EVMC_SUCCESS;
constexpr int64_t kC6Depth0GasLeft = 100'000;

// BASELINE(call-target-resolver): C6 BLS 0x0b at CANCUN (inactive) — depth=1
constexpr evmc_status_code kC6Depth1Status = EVMC_SUCCESS;
constexpr int64_t kC6Depth1GasLeft = 100'000;

// BASELINE(call-target-resolver): C7 [PRECOMPILED] non-empty code asymmetry — depth=0 (vm.execute)
constexpr evmc_status_code kC7Depth0Status = EVMC_STACK_UNDERFLOW;

// BASELINE(call-target-resolver): C7 [PRECOMPILED] non-empty code asymmetry — depth=1 (chain hook)
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

CallOutcome runDepth0EmptyCall(InnerExecuteInput input)
{
    auto const recipient = std::memcmp(input.message.code_address.bytes, evmc_address{}.bytes,
                               sizeof(evmc_address{}.bytes)) != 0 ?
                               input.message.code_address :
                               input.message.recipient;
    auto* statePtr = input.state;
    auto output = innerExecute(std::move(input));
    CallOutcome outcome;
    outcome.status = output.result.status_code;
    outcome.gasLeft = output.result.gas_left;
    if (statePtr != nullptr)
    {
        outcome.recipientBalance = statePtr->get_balance(recipient);
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

    Depth1HostFixture(state::State& state, state::EvmHostHooks* extension = nullptr,
        evmc_revision revision = EVMC_PRAGUE, ChainCallTargetDispatcher* chainPort = nullptr)
    {
        txContext.block_gas_limit = 30'000'000;
        cfg = {.revision = revision, .eip2929 = true};
        host.emplace(state, txContext, cfg, vm, emptyBlockHashes(), extension, false, chainPort);
    }

    state::EthHost& ethHost() { return *host; }
};

InnerExecuteInput makeBaseInput(state::State& state, evmc_message const& message,
    state::EvmHostHooks* extension = nullptr, ChainCallTargetDispatcher* chainPort = nullptr)
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
    input.extension = extension;
    input.chainPort = chainPort;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(c1_identity_precompile_depth0_and_depth1)
{
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

    state::State state(view);
    state.set_balance(sender, senderAccount.balance);
    auto depth0 = runDepth0EmptyCall(makeBaseInput(state, message));
    BOOST_CHECK_EQUAL(depth0.status, kC1Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC1Depth0GasLeft);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, senderAccount.balance);
    Depth1HostFixture fixture(state1, nullptr);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state1, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC1Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC1Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c2_op_l1block_chain_hook_depth0_and_depth1)
{
    auto calldata = setterSelector();

    state::test::InMemoryStateView baseState;
    state::State state0(baseState);
    OpStackChainCallTargetAdapter chainAdapter0(&state0, 0, makeIsthmusPlusForkSchedule(), 0);
    state0.set_balance(OP_DEPOSITOR_ACCOUNT, 1'000'000);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 300'000;
    message.sender = OP_DEPOSITOR_ACCOUNT;
    message.recipient = OP_L1_BLOCK_PREDEPLOY;
    message.code_address = OP_L1_BLOCK_PREDEPLOY;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    auto depth0 = runDepth0EmptyCall(makeBaseInput(state0, message, nullptr, &chainAdapter0));
    BOOST_CHECK_EQUAL(depth0.status, kC2Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC2Depth0GasLeft);

    state::test::InMemoryStateView baseState1;
    state::State state1(baseState1);
    OpStackChainCallTargetAdapter chainAdapter1(&state1, 0, makeIsthmusPlusForkSchedule(), 0);
    state1.set_balance(OP_DEPOSITOR_ACCOUNT, 1'000'000);
    Depth1HostFixture fixture(state1, nullptr, EVMC_PRAGUE, &chainAdapter1);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state1, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC2Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC2Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c3_empty_eoa_depth0_and_depth1)
{
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

    state::State state(view);
    state.set_balance(sender, senderAccount.balance);
    auto depth0 = runDepth0EmptyCall(makeBaseInput(state, message));
    BOOST_CHECK_EQUAL(depth0.status, kC3Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC3Depth0GasLeft);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, senderAccount.balance);
    Depth1HostFixture fixture(state1, nullptr);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state1, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC3Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC3Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c4_delegatecall_to_precompile_blocked_at_depth1)
{
    auto const caller = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);

    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(caller, 1'000'000);
    FiscoVmHostPolicy extension(/*skipEvmNativeValueTransfer*/ true, {});
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

    auto depth0 = runDepth0EmptyCall(makeBaseInput(state0, message));
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
    auto const sender = addressFromLastByte(0x01);
    auto const bls = precompileAddress(0x0b);
    bcos::evm_standard::RevisionConfig cancunCfg{};
    cancunCfg.revision = EVMC_CANCUN;
    BOOST_CHECK(!precompiled::isActivePrecompile(cancunCfg, bls));

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

    state::State state(view);
    state.set_balance(sender, senderAccount.balance);
    auto input = makeBaseInput(state, message);
    input.revisionConfig.revision = EVMC_CANCUN;
    auto depth0 = runDepth0EmptyCall(std::move(input));
    BOOST_CHECK_EQUAL(depth0.status, kC6Depth0Status);
    BOOST_CHECK_EQUAL(depth0.gasLeft, kC6Depth0GasLeft);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, senderAccount.balance);
    Depth1HostFixture fixture(state1, nullptr, EVMC_CANCUN);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state1, fixture.ethHost(), depth1Msg);
    BOOST_CHECK_EQUAL(depth1.status, kC6Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC6Depth1GasLeft);
}

BOOST_AUTO_TEST_CASE(c7_precompiled_marker_asymmetry_depth0_vs_depth1)
{
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

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = markerContract;
    message.code_address = markerContract;

    CallOutcome depth0Outcome;
    // depth=0: non-empty code bypasses chain classify → EVM executes marker bytecode
    {
        state::test::InMemoryStateView depth0View;
        depth0View.insert_account(sender, senderAccount);
        depth0View.insert_account(markerContract, markerAccount);
        state::State depth0State(depth0View);
        InMemoryChainPrecompileAdapter dispatchPort(
            [&callbackInvoked](evmc_revision /*rev*/, const evmc_message& /*msg*/) {
                callbackInvoked = true;
                evmc_result result{};
                result.status_code = EVMC_SUCCESS;
                return std::optional<evmc_result>{result};
            });
        FiscoChainCallTargetAdapter chainAdapter(depth0State, dispatchPort);
        depth0Outcome =
            runDepth0EmptyCall(makeBaseInput(depth0State, message, nullptr, &chainAdapter));
        BOOST_CHECK(!callbackInvoked);
        BOOST_CHECK_EQUAL(depth0Outcome.status, kC7Depth0Status);
        BOOST_CHECK(depth0Outcome.status != kC7Depth1Status);
    }

    // depth=1: chain classify runs before EVM regardless of non-empty code
    callbackInvoked = false;
    state::test::InMemoryStateView view1;
    state::State state(view1);
    state.set_balance(sender, senderAccount.balance);
    state.set_code(markerContract, markerAccount.code, {});
    InMemoryChainPrecompileAdapter dispatchPort(callback);
    FiscoChainCallTargetAdapter chainAdapter(state, dispatchPort);
    Depth1HostFixture fixture(state, nullptr, EVMC_PRAGUE, &chainAdapter);

    evmc_message depth1Msg = message;
    depth1Msg.depth = 1;
    auto depth1 = runDepth1EmptyCall(state, fixture.ethHost(), depth1Msg);
    BOOST_CHECK(callbackInvoked);
    BOOST_CHECK_EQUAL(depth1.status, kC7Depth1Status);
    BOOST_CHECK_EQUAL(depth1.gasLeft, kC7Depth1GasLeft);
    BOOST_CHECK(depth0Outcome.status != depth1.status);
}

BOOST_AUTO_TEST_CASE(pr5_op_l1block_chain_static_warm_tx_entry_oracle)
{
    auto isL1BlockWarmAfterTxEntry = [](bool withStaticWarm) {
        state::test::InMemoryStateView view;
        state::State state(view);

        OpStackChainCallTargetAdapter opAdapter(&state, 0, makeIsthmusPlusForkSchedule(), 0);
        InMemoryChainCallTargetAdapter port(
            [&opAdapter](state::State& s, evmc_address const& a, evmc_message const& m,
                execution::FrameScope scope) { return opAdapter.classifyTarget(s, a, m, scope); },
            [&opAdapter](
                evmc_revision r, evmc_message const& m) { return opAdapter.dispatch(r, m); });
        if (withStaticWarm)
        {
            port.addStaticWarmTarget(OP_L1_BLOCK_PREDEPLOY);
            port.addStaticWarmTarget(OP_GAS_PRICE_ORACLE_PREDEPLOY);
        }

        bcos::evm_standard::RevisionConfig cfg{};
        cfg.revision = EVMC_PRAGUE;
        cfg.eip2929 = true;

        state::Transaction tx{};
        state::BlockInfo block{};
        state::TransactionProperties props{};
        props.warmDestination = false;
        props.warmCoinbase = false;
        execution::warmTransactionEntry(state, cfg, &port, tx, block, props);
        return state.is_address_warm(OP_L1_BLOCK_PREDEPLOY);
    };

    BOOST_CHECK(!isL1BlockWarmAfterTxEntry(false));
    BOOST_CHECK(isL1BlockWarmAfterTxEntry(true));
}

}  // namespace bcos::evm::test
