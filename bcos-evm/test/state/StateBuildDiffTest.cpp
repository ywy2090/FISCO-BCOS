/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#define BOOST_TEST_MODULE StateBuildDiffTest
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::state::test
{
namespace
{
evmc_address addrFromLastByte(uint8_t b)
{
    evmc_address a{};
    a.bytes[19] = b;
    return a;
}

evmc_bytes32 slotKey(uint8_t lastByte)
{
    evmc_bytes32 key{};
    key.bytes[31] = lastByte;
    return key;
}

evmc_bytes32 slotValue(uint8_t lastByte)
{
    evmc_bytes32 value{};
    value.bytes[31] = lastByte;
    return value;
}

bool bytes32Equal(evmc_bytes32 const& lhs, evmc_bytes32 const& rhs)
{
    return Bytes32Equal{}(lhs, rhs);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(StateBuildDiffTest)

BOOST_AUTO_TEST_CASE(storage_write_then_restore_is_omitted_from_diff)
{
    InMemoryStateView view;
    evmc_address const addr = addrFromLastByte(0x01);
    evmc_bytes32 const key = slotKey(1);

    State state(view);
    state.set_storage(addr, key, slotValue(1));
    state.set_storage(addr, key, evmc_bytes32{});

    auto const diff = state.build_diff();
    auto it = diff.accounts.find(addr);
    if (it != diff.accounts.end())
    {
        BOOST_CHECK(it->second.storage.find(key) == it->second.storage.end());
    }
}

BOOST_AUTO_TEST_CASE(storage_clear_exports_explicit_zero)
{
    InMemoryStateView view;
    evmc_address const addr = addrFromLastByte(0x02);
    evmc_bytes32 const key = slotKey(2);
    evmc_bytes32 const original = slotValue(5);

    Account base;
    base.storage[key] = original;
    view.insert_account(addr, base);

    State state(view);
    state.set_storage(addr, key, evmc_bytes32{});

    auto const diff = state.build_diff();
    auto const it = diff.accounts.find(addr);
    BOOST_REQUIRE(it != diff.accounts.end());
    auto const slotIt = it->second.storage.find(key);
    BOOST_REQUIRE(slotIt != it->second.storage.end());
    BOOST_CHECK(bytes32Equal(slotIt->second, evmc_bytes32{}));
}

BOOST_AUTO_TEST_CASE(copied_base_storage_without_sstore_is_omitted)
{
    InMemoryStateView view;
    evmc_address const addr = addrFromLastByte(0x03);
    evmc_bytes32 const key = slotKey(3);

    Account base;
    base.balance = 10;
    base.storage[key] = slotValue(9);
    view.insert_account(addr, base);

    State state(view);
    state.set_balance(addr, 11);

    auto const diff = state.build_diff();
    auto const it = diff.accounts.find(addr);
    BOOST_REQUIRE(it != diff.accounts.end());
    BOOST_CHECK(it->second.balanceDirty);
    BOOST_CHECK_EQUAL(it->second.balance, 11U);
    BOOST_CHECK(it->second.storage.find(key) == it->second.storage.end());
}

BOOST_AUTO_TEST_CASE(storage_change_exports_only_modified_slot)
{
    InMemoryStateView view;
    evmc_address const addr = addrFromLastByte(0x04);
    evmc_bytes32 const keyA = slotKey(4);
    evmc_bytes32 const keyB = slotKey(5);

    Account base;
    base.storage[keyA] = slotValue(1);
    base.storage[keyB] = slotValue(2);
    view.insert_account(addr, base);

    State state(view);
    state.set_storage(addr, keyA, slotValue(3));

    auto const diff = state.build_diff();
    auto const it = diff.accounts.find(addr);
    BOOST_REQUIRE(it != diff.accounts.end());
    BOOST_REQUIRE(it->second.storage.find(keyA) != it->second.storage.end());
    BOOST_CHECK(bytes32Equal(it->second.storage.at(keyA), slotValue(3)));
    BOOST_CHECK(it->second.storage.find(keyB) == it->second.storage.end());
}

BOOST_AUTO_TEST_CASE(revert_does_not_change_tx_start_original)
{
    InMemoryStateView view;
    evmc_address const addr = addrFromLastByte(0x05);
    evmc_bytes32 const key = slotKey(6);

    State state(view);
    state.set_storage(addr, key, slotValue(1));

    state.checkpoint();
    state.set_storage(addr, key, slotValue(2));
    state.revert();

    state.set_storage(addr, key, slotValue(3));

    auto const diff = state.build_diff();
    auto const it = diff.accounts.find(addr);
    BOOST_REQUIRE(it != diff.accounts.end());
    auto const slotIt = it->second.storage.find(key);
    BOOST_REQUIRE(slotIt != it->second.storage.end());
    BOOST_CHECK(bytes32Equal(slotIt->second, slotValue(3)));
}

BOOST_AUTO_TEST_CASE(block_end_style_clear_exports_zero_against_post_tx_base)
{
    InMemoryStateView view;
    evmc_address const addr = addrFromLastByte(0x06);
    evmc_bytes32 const key = slotKey(7);

    Account postTx;
    postTx.storage[key] = slotValue(1);
    view.insert_account(addr, postTx);

    State state(view);
    state.set_storage(addr, key, evmc_bytes32{});

    auto const diff = state.build_diff();
    auto const it = diff.accounts.find(addr);
    BOOST_REQUIRE(it != diff.accounts.end());
    auto const slotIt = it->second.storage.find(key);
    BOOST_REQUIRE(slotIt != it->second.storage.end());
    BOOST_CHECK(bytes32Equal(slotIt->second, evmc_bytes32{}));
}

BOOST_AUTO_TEST_CASE(new_overlay_empty_account_is_exported)
{
    InMemoryStateView view;
    evmc_address const addr = addrFromLastByte(0x07);

    State state(view);
    state.touchOverlayAccount(addr);

    auto const diff = state.build_diff();
    BOOST_REQUIRE(diff.accounts.find(addr) != diff.accounts.end());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
