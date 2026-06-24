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
 */

#define BOOST_TEST_MODULE ExecuteMessageSmokeTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(empty_account_call_smoke)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = nullptr;
    message.input_size = 0;

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};

    ExecuteMessageInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;

    auto output = executeMessage(std::move(input));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK(output.logs.empty());
}

BOOST_AUTO_TEST_CASE(top_level_revert_still_bumps_sender_nonce_in_state_diff)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x03);
    auto const target = addressFromLastByte(0x04);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    senderAccount.nonce = 1;
    stateView.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = {0x60, 0x00, 0x60, 0x00, 0xfd};  // PUSH0 PUSH0 REVERT
    stateView.insert_account(target, targetAccount);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.depth = 0;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};

    state::State state(stateView);

    ExecuteMessageInput input;
    input.stateView = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;

    auto output = executeMessage(std::move(input));
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_REVERT);

    auto const diffIt = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(diffIt != output.stateDiff.accounts.end());
    BOOST_CHECK_EQUAL(diffIt->second.nonce, 2U);
}

}  // namespace bcos::evm::test
