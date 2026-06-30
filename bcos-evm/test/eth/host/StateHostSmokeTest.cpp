/*
 *  Copyright (C) 2026 FISCO BCOS.
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

#define BOOST_TEST_MODULE StateHostSmokeTest
#include "helpers/InMemoryStateView.h"
#include "helpers/Transition.hpp"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::state::test
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

BOOST_AUTO_TEST_SUITE(StateHostSmokeTest)

BOOST_AUTO_TEST_CASE(empty_account_call_returns_success)
{
    InMemoryStateView stateView;

    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    Transaction tx;
    tx.from = sender;
    tx.to = target;
    tx.gasLimit = 21'000;
    tx.gasPrice = 0;
    tx.value = 0;

    BlockInfo block;
    block.gasLimit = 30'000'000;

    BlockHashes blockHashes = [](int64_t) { return evmc_bytes32{}; };

    evmc::VM vm{evmc_create_evmone()};
    TransactionProperties props;

    auto const receipt =
        transition(stateView, block, blockHashes, tx, EVMC_CANCUN, vm, props, nullptr);

    BOOST_CHECK_EQUAL(receipt.status, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::state::test
