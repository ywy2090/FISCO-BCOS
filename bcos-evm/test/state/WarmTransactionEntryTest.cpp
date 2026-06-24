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

#define BOOST_TEST_MODULE WarmTransactionEntryTest
#include "bcos-evm/eth/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/execution/BlockInfoBuilder.h"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::state::test
{
namespace
{
evmc_address evmcAddressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

h160 bcosAddressFromLastByte(uint8_t value)
{
    h160 address;
    address[19] = value;
    return address;
}

evmc_bytes32 bytes32FromLastByte(uint8_t value)
{
    evmc_bytes32 out{};
    out.bytes[31] = value;
    return out;
}

h256 h256FromLastByte(uint8_t value)
{
    h256 key;
    key[31] = value;
    return key;
}

evmc_bytes32 toEvmcBytes32(const h256& value)
{
    evmc_bytes32 out{};
    std::memcpy(out.bytes, value.data(), sizeof(out.bytes));
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(WarmTransactionEntryTest)

BOOST_AUTO_TEST_CASE(warms_sender_to_and_coinbase_for_call_transaction)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx;
    tx.from = evmcAddressFromLastByte(0x01);
    tx.to = evmcAddressFromLastByte(0x02);

    auto const coinbase = evmcAddressFromLastByte(0x03);
    auto const block = execution::BlockInfoBuilder().coinbase(coinbase).build();

    TransactionProperties props;
    execution::warmTransactionEntry(state, EVMC_SHANGHAI, tx, block, props, true);

    BOOST_CHECK(state.is_address_warm(tx.from));
    BOOST_REQUIRE(tx.to.has_value());
    BOOST_CHECK(state.is_address_warm(*tx.to));
    BOOST_CHECK(state.is_address_warm(block.coinbase));
}

BOOST_AUTO_TEST_CASE(warms_access_list_address_and_storage_keys)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx;
    tx.from = evmcAddressFromLastByte(0x11);
    tx.to = evmcAddressFromLastByte(0x12);

    auto const coinbase = evmcAddressFromLastByte(0x13);
    auto const block = execution::BlockInfoBuilder().coinbase(coinbase).build();

    auto const accessAddress = bcosAddressFromLastByte(0x21);
    auto const keyA = h256FromLastByte(0xA1);
    auto const keyB = h256FromLastByte(0xB1);
    Eip2930AccessList accessList{{accessAddress, {keyA, keyB}}};

    TransactionProperties props;
    execution::warmTransactionEntry(
        state, EVMC_SHANGHAI, tx, block, props, true, &accessList, /*web3TypedTxKind=*/1);

    auto const evmcAccessAddress = evmcAddressFromLastByte(0x21);
    BOOST_CHECK(state.is_address_warm(evmcAccessAddress));
    BOOST_CHECK(state.is_storage_warm(evmcAccessAddress, toEvmcBytes32(keyA)));
    BOOST_CHECK(state.is_storage_warm(evmcAccessAddress, toEvmcBytes32(keyB)));
}

BOOST_AUTO_TEST_CASE(legacy_kind_zero_ignores_access_list)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx;
    tx.from = evmcAddressFromLastByte(0x31);
    tx.to = evmcAddressFromLastByte(0x32);

    auto const block = execution::BlockInfoBuilder().build();
    auto const accessAddress = bcosAddressFromLastByte(0x41);
    Eip2930AccessList accessList{{accessAddress, {}}};

    TransactionProperties props;
    execution::warmTransactionEntry(
        state, EVMC_SHANGHAI, tx, block, props, true, &accessList, /*web3TypedTxKind=*/0);

    auto const evmcAccessAddress = evmcAddressFromLastByte(0x41);
    BOOST_CHECK(!state.is_address_warm(evmcAccessAddress));
}

BOOST_AUTO_TEST_CASE(builds_block_info_with_expected_fields)
{
    auto const coinbase = evmcAddressFromLastByte(0x44);
    auto const prevRandao = bytes32FromLastByte(0x99);
    auto const block = execution::BlockInfoBuilder()
                           .number(101)
                           .timestamp(202)
                           .gasLimit(303)
                           .coinbase(coinbase)
                           .prevRandao(prevRandao)
                           .baseFee(404)
                           .chainId(505)
                           .build();

    BOOST_CHECK_EQUAL(block.number, 101);
    BOOST_CHECK_EQUAL(block.timestamp, 202);
    BOOST_CHECK_EQUAL(block.gasLimit, 303);
    BOOST_CHECK_EQUAL(block.coinbase.bytes[19], 0x44);
    BOOST_CHECK_EQUAL(block.prevRandao.bytes[31], 0x99);
    BOOST_CHECK_EQUAL(block.baseFee, bcos::u256(404));
    BOOST_CHECK_EQUAL(block.chainId, bcos::u256(505));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
