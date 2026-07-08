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

// ── [B] bug-exposing: fail pre-fix (overlay materialization kills base reads) ──

// Scenario 1: value-transfer path (set_balance) materializes; SLOAD must read base.
BOOST_AUTO_TEST_CASE(sload_after_balance_materialization_reads_base)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.set_balance(kContract, 200);
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}

// Scenario 2: SSTORE-to-zero after materialization must export the clear in build_diff.
BOOST_AUTO_TEST_CASE(sstore_to_zero_after_materialization_exports_clear)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.set_balance(kContract, 200);
    state.set_storage(kContract, kSlotKey, evmc_bytes32{});

    auto const diff = state.build_diff();
    auto const accountIt = diff.accounts.find(kContract);
    BOOST_REQUIRE(accountIt != diff.accounts.end());
    auto const slotIt = accountIt->second.storage.find(kSlotKey);
    BOOST_REQUIRE(slotIt != accountIt->second.storage.end());  // pre-fix: absent (dropped)
    BOOST_CHECK(eq(slotIt->second, evmc_bytes32{}));
}

// Scenario 3: SSTORE of the SAME value must not export a spurious redundant write.
BOOST_AUTO_TEST_CASE(sstore_same_value_after_materialization_exports_nothing)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.set_balance(kContract, 200);
    state.set_storage(kContract, kSlotKey, kSlotValue);  // same as base

    auto const diff = state.build_diff();
    auto const accountIt = diff.accounts.find(kContract);
    BOOST_REQUIRE(accountIt != diff.accounts.end());  // balanceDirty keeps the account
    BOOST_CHECK(accountIt->second.storage.find(kSlotKey) == accountIt->second.storage.end());
}

// Scenario 4: TSTORE (EIP-1153) materializes — pure code path, no value transfer.
BOOST_AUTO_TEST_CASE(sload_after_transient_store_materialization_reads_base)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.set_transient_storage(kContract, b32(0x22), b32(0x01));
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}

// Scenario 5: nonce bump (EIP-7702 applyAuthorizations path) materializes.
BOOST_AUTO_TEST_CASE(sload_after_nonce_materialization_reads_base)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.set_nonce(kContract, 8);
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}

// Scenario 6: set_code (EIP-7702 delegation install) materializes.
BOOST_AUTO_TEST_CASE(sload_after_code_materialization_reads_base)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.set_code(kContract, bcos::bytes{0xEF, 0x01, 0x00}, b32(0x33));
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}

// Scenario 7: mark_self_destructed materializes; code keeps executing post-SELFDESTRUCT (6780).
BOOST_AUTO_TEST_CASE(sload_after_selfdestruct_mark_reads_base)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.mark_self_destructed(kContract);
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}

// Scenario 8: touchOverlayAccount (precompile CALL target) materializes.
BOOST_AUTO_TEST_CASE(sload_after_touch_materialization_reads_base)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.touchOverlayAccount(kContract);
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}

// Scenario 9: tx-start original correctness, observable shape (storageOriginalAtTxStart is
// private): materialize FIRST, write 9, write back the original 5 → diff must NOT export
// the slot. Pre-fix the original is mis-cached as 0, so the final 5 looks like a change
// and a spurious write is exported. Materialize-first is load-bearing: an SSTORE-first
// flow caches the correct original even pre-fix and would not distinguish.
BOOST_AUTO_TEST_CASE(original_value_correct_after_materialization_write_back)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.set_balance(kContract, 200);
    state.set_storage(kContract, kSlotKey, b32(0x09));
    state.set_storage(kContract, kSlotKey, kSlotValue);  // back to the true original

    auto const diff = state.build_diff();
    auto const accountIt = diff.accounts.find(kContract);
    BOOST_REQUIRE(accountIt != diff.accounts.end());
    BOOST_CHECK(accountIt->second.storage.find(kSlotKey) == accountIt->second.storage.end());
}

// Scenario 12: revert across a CREATE reset restores read-through.
// Journal subtlety: the snapshot is find(kContract) — a NON-NULL base copy (sparse ⇒
// empty storage), so revert RE-INSERTS it into the overlay rather than erasing; the
// restored copy has storageReset=false, so post-fix reads fall through to base.
BOOST_AUTO_TEST_CASE(revert_across_create_reset_restores_read_through)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.checkpoint();
    state.touchCreateDeploymentAccount(kContract, EVMC_CANCUN);
    state.revert();

    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}

// Scenario 13: nested frames — inner commit bubbles, outer revert restores read-through.
// [B], NOT a guard: identical journal mechanism to scenario 12 (inner commit only bubbles
// the touched set; the AccountSnapshot stays in the outer range, and outer revert
// re-inserts the empty-storage base copy) — so pre-fix this reads 0 and FAILS.
BOOST_AUTO_TEST_CASE(nested_commit_then_outer_revert_restores_read_through)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.checkpoint();  // outer
    state.checkpoint();  // inner
    state.touchCreateDeploymentAccount(kContract, EVMC_CANCUN);
    state.commit();  // inner commit bubbles touched set to outer
    state.revert();  // outer revert must undo the reset

    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), kSlotValue));
}
}  // namespace bcos::evm::state::test
