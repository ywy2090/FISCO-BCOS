/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Regression suite for overlay storage read-through under a sparse (ledger-shaped)
 *        base view. See docs/superpowers/specs/2026-07-08-overlay-storage-read-through-design.md.
 */

#define BOOST_TEST_MODULE SparseStorageOverlayTest
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/SparseStorageStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::state::test
{
namespace
{
evmc_address addr(uint8_t tail)
{
    evmc_address out{};
    out.bytes[19] = tail;
    return out;
}

evmc_bytes32 b32(uint8_t tail)
{
    evmc_bytes32 out{};
    out.bytes[31] = tail;
    return out;
}

bool eq(evmc_bytes32 const& lhs, evmc_bytes32 const& rhs)
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

evmc_address const kContract = addr(0xC1);
evmc_bytes32 const kSlotKey = b32(0x11);
evmc_bytes32 const kSlotValue = b32(0x05);

/// Base view: contract with balance 100, nonce 7, and ledger slot kSlotKey = 5.
SparseStorageStateView makeBaseWithSlot()
{
    SparseStorageStateView view;
    Account account;
    account.balance = 100;
    account.nonce = 7;
    view.insert_account(kContract, std::move(account));
    view.set_slot(kContract, kSlotKey, kSlotValue);
    return view;
}
}  // namespace

// Helper contract sanity: account loads carry NO storage; slots are served per-slot.
BOOST_AUTO_TEST_CASE(helper_contract_account_has_no_storage_slot_served_separately)
{
    auto const view = makeBaseWithSlot();

    auto const account = view.get_account(kContract);
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK(account->storage.empty());
    BOOST_CHECK_EQUAL(account->balance, 100);

    BOOST_CHECK(eq(view.get_storage(kContract, kSlotKey), kSlotValue));
    BOOST_CHECK(eq(view.get_storage(kContract, b32(0x99)), evmc_bytes32{}));
}
}  // namespace bcos::evm::state::test
