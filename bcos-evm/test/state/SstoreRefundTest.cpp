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

#define BOOST_TEST_MODULE SstoreRefundTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <string_view>

namespace bcos::evm::state::test
{
namespace
{
constexpr std::string_view kSstoreClearBytecode = "600060005500";  // PUSH1 0 PUSH1 0 SSTORE STOP

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_bytes32 valueFromLastByte(uint8_t value)
{
    evmc_bytes32 out{};
    out.bytes[31] = value;
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SstoreRefundTest)

BOOST_AUTO_TEST_CASE(EthHost_sstoreClear_accumulates4800)
{
    InMemoryEvmStateReader view;
    auto const contract = addressFromLastByte(0x01);
    auto const sender = addressFromLastByte(0xaa);
    auto const slotKey = evmc_bytes32{};
    auto const nonZeroValue = valueFromLastByte(0x01);

    Account contractAccount;
    contractAccount.code = bcos::fromHex(kSstoreClearBytecode);
    contractAccount.storage[slotKey] = nonZeroValue;
    view.insert_account(contract, contractAccount);

    State state(view);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 1'000'000;
    message.sender = sender;
    message.recipient = contract;
    message.code_address = contract;

    BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};

    ExecuteMessageInput input;
    input.stateView = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.revision = EVMC_LONDON;
    input.fixStorageStatus = true;

    auto output = executeMessage(std::move(input));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK(Bytes32Equal{}(state.get_storage(contract, slotKey), evmc_bytes32{}));
    BOOST_CHECK_EQUAL(state.get_refund(), 4800u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::state::test
