/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief TxExecutionAdapter PR-A characterization tests.
 * @file TxExecutionAdapterTest.cpp
 */

#define BOOST_TEST_MODULE TxExecutionAdapterTest

#include "bcos-evm/eth/execution/TxExecutionAdapter.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "fixtures/EthFrameParityHelpers.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
using bcos::evm::ExecuteMessageInput;
using bcos::evm::ExecuteMessageOutput;
using bcos::evm::SetCodeAuthorization;
using bcos::evm::execution::TxExecutionAdapter;

ExecuteMessageInput makePragueCallInput(
    state::State& state, evmc_message message, bcos::evm_standard::RevisionConfig cfg = {})
{
    auto input = makeBaseInput(state, message);
    if (cfg.revision != EVMC_FRONTIER)
    {
        input.revisionConfig = cfg;
    }
    return input;
}

evmc_message callMessage(evmc_address sender, evmc_address target, int64_t depth = 0)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.depth = depth;
    message.gas = 200'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    return message;
}
}  // namespace

BOOST_AUTO_TEST_CASE(null_state_throws_invalid_argument)
{
    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.state = nullptr;
    input.vm = &vm;
    BOOST_CHECK_THROW(TxExecutionAdapter::run(std::move(input)), std::invalid_argument);
}

// Matrix: T02 — state ownership contract: mutations visible on caller's State (VM frame path).
BOOST_AUTO_TEST_CASE(top_level_success_bumps_sender_nonce)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x11);
    auto const target = addressFromLastByte(0x12);
    bcos::bytes stopCode{0x00};

    state::State state(stateView);
    state.set_balance(sender, 1'000'000);
    state.set_nonce(sender, 3);
    state.set_code(target, stopCode,
        state::keccak256Code(bcos::bytesConstRef{stopCode.data(), stopCode.size()}));

    auto input = makePragueCallInput(state, callMessage(sender, target));

    auto const output = TxExecutionAdapter::run(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_nonce(sender), 4U);
}

// Matrix: T04 — skipTopLevelSenderNonceBump preserves sender nonce (OpStack deposit path).
BOOST_AUTO_TEST_CASE(skip_top_level_sender_nonce_bump_flag)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x21);
    auto const target = addressFromLastByte(0x22);
    bcos::bytes stopCode{0x00};

    state::State state(stateView);
    state.set_balance(sender, 1'000'000);
    state.set_nonce(sender, 5);
    state.set_code(target, stopCode,
        state::keccak256Code(bcos::bytesConstRef{stopCode.data(), stopCode.size()}));

    auto input = makePragueCallInput(state, callMessage(sender, target));
    input.skipTopLevelSenderNonceBump = true;

    auto const output = TxExecutionAdapter::run(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_nonce(sender), 5U);
}

// Matrix: T03 — EIP-7702 auth pre-bump + finalize mutual exclusion (characterization).
BOOST_AUTO_TEST_CASE(eip7702_auth_prebump_characterization)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x31);
    auto const recipient = addressFromLastByte(0x32);
    auto const delegationTarget = addressFromLastByte(0x42);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    state::State state(stateView);
    auto input = makePragueCallInput(state, callMessage(sender, recipient));
    input.revisionConfig.eip7702 = true;
    input.blockInfo.chainId = 1;
    input.authorizationListPresent = true;
    input.authorizations.push_back(SetCodeAuthorization{
        .chainId = bcos::u256(1), .authority = sender, .address = delegationTarget, .nonce = 1});

    auto const output = TxExecutionAdapter::run(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    BOOST_CHECK_EQUAL(it->second.nonce, uint64_t(2));
    BOOST_REQUIRE_EQUAL(it->second.code.size(), size_t(23));
    BOOST_CHECK_EQUAL(it->second.code[0], 0xEF);
}

// Matrix: T01 — precompile hit returns diff without top-level commit finalize path.
BOOST_AUTO_TEST_CASE(precompile_hit_returns_state_diff)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x41);
    auto const identity = precompileAddress(0x04);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    state::State state(stateView);
    evmc_message message = callMessage(sender, identity);
    message.value = weiValue(100);

    auto input = makePragueCallInput(state, message);
    auto const output = TxExecutionAdapter::run(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const recipientIt = output.stateDiff.accounts.find(identity);
    BOOST_REQUIRE(recipientIt != output.stateDiff.accounts.end());
    BOOST_CHECK(recipientIt->second.balance >= bcos::u256(100));
}

// Matrix: T07 — REVERT characterization (included-tx nonce semantics under review).
BOOST_AUTO_TEST_CASE(top_level_revert_nonce_characterization)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x51);
    auto const target = addressFromLastByte(0x52);

    state::Account senderAccount;
    senderAccount.nonce = 1;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = {0x60, 0x00, 0x60, 0x00, 0xfd};
    stateView.insert_account(target, targetAccount);

    state::State state(stateView);
    auto input = makePragueCallInput(state, callMessage(sender, target));

    auto const output = TxExecutionAdapter::run(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_REVERT);

    auto const diffIt = output.stateDiff.accounts.find(sender);
    if (diffIt != output.stateDiff.accounts.end())
    {
        BOOST_CHECK_EQUAL(diffIt->second.nonce, 1U);
    }
    else
    {
        BOOST_CHECK_EQUAL(state.get_nonce(sender), 1U);
    }
}

// Matrix: T06 — CREATE skips EIP-7702 tx auth apply on sender.
BOOST_AUTO_TEST_CASE(create_skips_eip7702_tx_auth_apply)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x61);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    bcos::bytes initCode{0x60, 0x80, 0x60, 0x40, 0x52, 0x60, 0x04, 0x60, 0x1c, 0x60, 0x00, 0x39};
    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.gas = 500'000;
    message.sender = sender;
    message.input_data = initCode.data();
    message.input_size = initCode.size();

    state::State state(stateView);
    auto input = makePragueCallInput(state, message);
    input.revisionConfig.eip7702 = true;
    input.authorizationListPresent = true;
    input.authorizations.push_back(SetCodeAuthorization{.chainId = bcos::u256(1),
        .authority = sender,
        .address = addressFromLastByte(0x99),
        .nonce = 1});

    auto const output = TxExecutionAdapter::run(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    BOOST_CHECK(it->second.code.empty());
    BOOST_CHECK_EQUAL(it->second.nonce, 1U);
}

// Matrix: T08 — nested depth does not apply top-level sender nonce bump.
BOOST_AUTO_TEST_CASE(nested_success_skips_top_level_sender_nonce_bump)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x71);
    auto const target = addressFromLastByte(0x72);

    state::Account senderAccount;
    senderAccount.nonce = 7;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(target, state::Account{});

    state::State state(stateView);
    auto input = makePragueCallInput(state, callMessage(sender, target, /*depth=*/1));

    auto const output = TxExecutionAdapter::run(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(state.get_nonce(sender), 7U);

    auto const diffIt = output.stateDiff.accounts.find(sender);
    if (diffIt != output.stateDiff.accounts.end())
    {
        BOOST_CHECK_EQUAL(diffIt->second.nonce, 7U);
    }
}

// Matrix: T09 — executeMessage delegator matches TxExecutionAdapter::run.
BOOST_AUTO_TEST_CASE(execute_message_delegates_to_adapter)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x81);
    auto const target = addressFromLastByte(0x82);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    state::State state(stateView);
    auto message = callMessage(sender, target);
    auto const viaAdapter = TxExecutionAdapter::run(makePragueCallInput(state, message));

    state::State stateAgain(stateView);
    auto input = makePragueCallInput(stateAgain, message);
    auto const viaFacade = bcos::evm::executeMessage(std::move(input));

    BOOST_CHECK_EQUAL(viaAdapter.result.status_code, viaFacade.result.status_code);
    BOOST_CHECK_EQUAL(viaAdapter.gasRefund, viaFacade.gasRefund);
    BOOST_CHECK_EQUAL(viaAdapter.logs.size(), viaFacade.logs.size());
}

}  // namespace bcos::evm::test
