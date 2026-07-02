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

#define BOOST_TEST_MODULE PrepareStateTest
#include "bcos-evm/eth/kernel/execution/PrepareState.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos/adapters/InMemoryChainCallTargetAdapter.h"
#include "fixtures/BlockInfoBuilder.h"
#include "helpers/InMemoryStateView.h"
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

BOOST_AUTO_TEST_SUITE(PrepareStateTest)

BOOST_AUTO_TEST_CASE(warms_sender_to_and_coinbase_for_call_transaction)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx;
    tx.from = evmcAddressFromLastByte(0x01);
    tx.to = evmcAddressFromLastByte(0x02);

    auto const coinbase = evmcAddressFromLastByte(0x03);
    auto const block = bcos::evm::test::BlockInfoBuilder().coinbase(coinbase).build();

    auto cfg = bcos::evm::revisionConfigFromRevision(EVMC_SHANGHAI);
    execution::prepareState(state, cfg, nullptr, tx, block);

    BOOST_CHECK(state.is_address_warm(tx.from));
    BOOST_REQUIRE(tx.to.has_value());
    BOOST_CHECK(state.is_address_warm(*tx.to));
    BOOST_CHECK(state.is_address_warm(block.coinbase));
}

BOOST_AUTO_TEST_CASE(skips_coinbase_warm_when_eip3651_disabled)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx;
    tx.from = evmcAddressFromLastByte(0x01);
    tx.to = evmcAddressFromLastByte(0x02);

    // Avoid low addresses that overlap active precompile slots warmed at BERLIN+.
    auto const coinbase = evmcAddressFromLastByte(0xFE);
    auto const block = bcos::evm::test::BlockInfoBuilder().coinbase(coinbase).build();

    auto cfg = bcos::evm::revisionConfigFromRevision(EVMC_PARIS);
    BOOST_REQUIRE(!cfg.eip3651);
    execution::prepareState(state, cfg, nullptr, tx, block);

    BOOST_CHECK(state.is_address_warm(tx.from));
    BOOST_REQUIRE(tx.to.has_value());
    BOOST_CHECK(state.is_address_warm(*tx.to));
    BOOST_CHECK(!state.is_address_warm(block.coinbase));
}

BOOST_AUTO_TEST_CASE(warms_access_list_address_and_storage_keys)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx;
    tx.from = evmcAddressFromLastByte(0x11);
    tx.to = evmcAddressFromLastByte(0x12);

    auto const coinbase = evmcAddressFromLastByte(0x13);
    auto const block = bcos::evm::test::BlockInfoBuilder().coinbase(coinbase).build();

    auto const accessAddress = bcosAddressFromLastByte(0x21);
    auto const keyA = h256FromLastByte(0xA1);
    auto const keyB = h256FromLastByte(0xB1);
    Eip2930AccessList accessList{{accessAddress, {keyA, keyB}}};

    auto cfg = bcos::evm::revisionConfigFromRevision(EVMC_SHANGHAI);
    execution::prepareState(state, cfg, nullptr, tx, block, &accessList, /*web3TypedTxKind=*/1);

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

    auto const block = bcos::evm::test::BlockInfoBuilder().build();
    auto const accessAddress = bcosAddressFromLastByte(0x41);
    Eip2930AccessList accessList{{accessAddress, {}}};

    auto cfg = bcos::evm::revisionConfigFromRevision(EVMC_SHANGHAI);
    execution::prepareState(state, cfg, nullptr, tx, block, &accessList, /*web3TypedTxKind=*/0);

    auto const evmcAccessAddress = evmcAddressFromLastByte(0x41);
    BOOST_CHECK(!state.is_address_warm(evmcAccessAddress));
}

BOOST_AUTO_TEST_CASE(builds_block_info_with_expected_fields)
{
    auto const coinbase = evmcAddressFromLastByte(0x44);
    auto const prevRandao = bytes32FromLastByte(0x99);
    auto const block = bcos::evm::test::BlockInfoBuilder()
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

BOOST_AUTO_TEST_CASE(W1_warms_active_builtin_precompiles)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx{};
    BlockInfo block{};

    auto cfg = bcos::evm::revisionConfigFromRevision(EVMC_CANCUN);
    execution::prepareState(state, cfg, nullptr, tx, block);

    auto const ecRecover = evmcAddressFromLastByte(0x01);
    auto const identity = evmcAddressFromLastByte(0x04);
    auto const bls = evmcAddressFromLastByte(0x0b);

    BOOST_CHECK(state.is_address_warm(ecRecover));
    BOOST_CHECK(state.is_address_warm(identity));
    BOOST_CHECK(!state.is_address_warm(bls));
}

BOOST_AUTO_TEST_CASE(W2_warms_chain_static_targets)
{
    evmc_address l1Block{};
    l1Block.bytes[18] = 0x42;
    l1Block.bytes[19] = 0x0A;
    evmc_address gasOracle{};
    gasOracle.bytes[18] = 0x42;
    gasOracle.bytes[19] = 0x0F;

    bcos::evm::test::InMemoryChainCallTargetAdapter adapter({}, {});
    adapter.addStaticWarmTarget(l1Block);
    adapter.addStaticWarmTarget(gasOracle);

    InMemoryStateView view;
    State state(view);

    Transaction tx{};
    BlockInfo block{};

    auto cfg = bcos::evm::revisionConfigFromRevision(EVMC_CANCUN);
    execution::prepareState(state, cfg, &adapter, tx, block);

    BOOST_CHECK(state.is_address_warm(l1Block));
    BOOST_CHECK(state.is_address_warm(gasOracle));
}

BOOST_AUTO_TEST_CASE(create_transaction_without_to_skips_destination_warm)
{
    InMemoryStateView view;
    State state(view);

    Transaction tx{};
    tx.from = evmcAddressFromLastByte(0x51);
    auto const wouldBeDestination = evmcAddressFromLastByte(0x52);
    auto const block = bcos::evm::test::BlockInfoBuilder().build();

    auto cfg = bcos::evm::revisionConfigFromRevision(EVMC_SHANGHAI);
    execution::prepareState(state, cfg, nullptr, tx, block);

    BOOST_CHECK(state.is_address_warm(tx.from));
    BOOST_CHECK(!tx.to.has_value());
    BOOST_CHECK(!state.is_address_warm(wouldBeDestination));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
