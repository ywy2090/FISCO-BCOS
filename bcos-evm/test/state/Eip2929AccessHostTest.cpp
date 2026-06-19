/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EthHost EIP-2929 access_account/access_storage compat (replaces ExecuteFrame host API).
 */

#define BOOST_TEST_MODULE Eip2929AccessHostTest

#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::state::test
{
namespace
{
evmc_address addressFromByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_bytes32 bytes32FromByte(uint8_t value)
{
    evmc_bytes32 out{};
    out.bytes[31] = value;
    return out;
}

BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Eip2929AccessHostTest)

BOOST_AUTO_TEST_CASE(access_account_cold_then_warm)
{
    InMemoryStateView view;
    State state(view);
    evmc_tx_context txContext{};
    evmc::VM vm{evmc_create_evmone()};
    EthHost host(state, txContext, EVMC_PRAGUE, vm, emptyBlockHashes(), nullptr, false);
    auto const addr = addressFromByte(0x11);

    BOOST_CHECK_EQUAL(host.access_account(addr), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.access_account(addr), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(access_storage_cold_then_warm)
{
    InMemoryStateView view;
    State state(view);
    evmc_tx_context txContext{};
    evmc::VM vm{evmc_create_evmone()};
    EthHost host(state, txContext, EVMC_PRAGUE, vm, emptyBlockHashes(), nullptr, false);
    auto const addr = addressFromByte(0x77);
    auto const key = bytes32FromByte(0x88);

    BOOST_CHECK_EQUAL(host.access_storage(addr, key), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.access_storage(addr, key), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(journal_revert_rolls_back_child_warm_address)
{
    InMemoryStateView view;
    State state(view);
    evmc_tx_context txContext{};
    evmc::VM vm{evmc_create_evmone()};
    BlockHashes blockHashes = emptyBlockHashes();
    EthHost host(state, txContext, EVMC_PRAGUE, vm, blockHashes, nullptr, false);

    auto const parentWarm = addressFromByte(0x75);
    auto const childCold = addressFromByte(0x74);

    state.checkpoint();
    BOOST_CHECK_EQUAL(host.access_account(parentWarm), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.access_account(parentWarm), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.access_account(childCold), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.access_account(childCold), EVMC_ACCESS_WARM);
    state.revert();

    BOOST_CHECK(!state.is_address_warm(childCold));
    BOOST_CHECK(!state.is_address_warm(parentWarm));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
