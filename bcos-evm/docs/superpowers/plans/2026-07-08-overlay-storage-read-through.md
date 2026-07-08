# Overlay Storage Read-Through Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore base-view read-through in `State::get_storage` for materialized (copy-on-write) accounts, suppressed only for storage-namespace-reset accounts via a new `Account::storageReset` flag — fixing production-only SLOAD-returns-0 / dropped-SSTORE-to-zero under `LedgerStateView`, and closing the sparse-storage-view test blind spot.

**Architecture:** One bool on `Account` (whole-`Account` journal snapshots give revert-correctness for free), set at exactly the two storage-namespace-reset sites (`touchCreateDeploymentAccount`, `clear_storage`), consulted in `get_storage`'s overlay-hit/key-miss branch and in `hasNonEmptyStorage`. A new `SparseStorageStateView` test helper mirrors the production `LedgerStateView` contract (account loads carry **no** storage; storage is served per-slot) so the regression suite exercises the exact production shape every existing test misses.

**Tech Stack:** C++20, Boost.Test (`boost/test/included/unit_test.hpp`), CMake/ctest. Build dir: `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build`.

**Spec:** `bcos-evm/docs/superpowers/specs/2026-07-08-overlay-storage-read-through-design.md` (revised after 4-lens review — read it before starting).

## Global Constraints

- Repo root: `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor` (all paths below relative to it; already the right worktree — do NOT create another).
- A concurrent work stream commits to this worktree. Stage **only** the files this plan names (`git add <explicit paths>`, never `git add .` / `-A`).
- The pre-commit hook runs clang-format and **fails the commit if it reformatted staged files**; on that failure, `git add` the same files again and re-run the same commit command.
- `Account.hpp` / `State.cpp` edits amend the concurrent stream's commit `8b1c2ef55` (which removed read-through); preserve its reset semantics exactly as specified — do not "improve" adjacent code.
- Ordering constraint (correctness): in `clear_storage` the flag is set **after** the zero-write loop (the loop's `set_storage` calls must still read pre-reset values so full-storage views export explicit zeros); in `touchCreateDeploymentAccount` the flag is set immediately **after** `account.storage.clear()`.
- Every test scenario seeds the base account with slot `K = 5` (see Task 1 helpers). **[B]** = bug-exposing (fails pre-fix), **[G]** = guard (passes pre- and post-fix).

---

### Task 1: SparseStorageStateView helper + suite scaffold + CMake wiring

**Files:**
- Create: `bcos-evm/test/helpers/SparseStorageStateView.h`
- Create: `bcos-evm/test/state/SparseStorageOverlayTest.cpp`
- Modify: `bcos-evm/test/cmake/StateTests.cmake` (append at end of file)

**Interfaces:**
- Consumes: `bcos::evm::state::StateView` (`bcos-evm/eth/state/StateView.hpp`), `Account` (`bcos-evm/eth/state/Account.hpp`).
- Produces (used verbatim by Tasks 2–4): class `bcos::evm::state::test::SparseStorageStateView` with `void insert_account(const evmc_address&, Account account = {})` and `void set_slot(const evmc_address&, const evmc_bytes32& key, const evmc_bytes32& value)`; test-file helpers `evmc_address addr(uint8_t)`, `evmc_bytes32 b32(uint8_t)`, `bool eq(evmc_bytes32 const&, evmc_bytes32 const&)`, and `SparseStorageStateView makeBaseWithSlot()` seeding account `kContract = addr(0xC1)` (balance 100, nonce 7) with slot `kSlotKey = b32(0x11)` → `kSlotValue = b32(0x05)`. ctest target name `SparseStorageOverlay`.

- [ ] **Step 1: Write the helper**

Create `bcos-evm/test/helpers/SparseStorageStateView.h`:

```cpp
/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Sparse StateView mirroring the production LedgerStateView contract:
 *        get_account() returns the account WITHOUT its storage map (the ledger never
 *        enumerates storage); get_storage() serves per-slot reads from a separate map.
 *        This is the exact shape that exposes the overlay read-through bug — the
 *        full-storage InMemoryStateView cannot reach the fallback branch by construction.
 * @file SparseStorageStateView.h
 */

#pragma once

#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include <unordered_map>

namespace bcos::evm::state::test
{
class SparseStorageStateView : public StateView
{
public:
    /// Seeds account fields. Any storage on @p account is deliberately DROPPED —
    /// per-slot values must be seeded via set_slot (LedgerStateView parity).
    void insert_account(const evmc_address& address, Account account = {})
    {
        account.storage.clear();
        m_accounts[address] = std::move(account);
    }

    void set_slot(const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value)
    {
        m_slots[address][key] = value;
    }

    std::optional<Account> get_account(const evmc_address& address) const override
    {
        auto const it = m_accounts.find(address);
        if (it == m_accounts.end())
        {
            return std::nullopt;
        }
        return it->second;  // storage map is empty by construction
    }

    evmc_bytes32 get_storage(const evmc_address& address, const evmc_bytes32& key) const override
    {
        auto const accountIt = m_slots.find(address);
        if (accountIt == m_slots.end())
        {
            return {};
        }
        auto const slotIt = accountIt->second.find(key);
        if (slotIt == accountIt->second.end())
        {
            return {};
        }
        return slotIt->second;
    }

private:
    std::unordered_map<evmc_address, Account, AddressHash, AddressEqual> m_accounts;
    std::unordered_map<evmc_address, StorageMap, AddressHash, AddressEqual> m_slots;
};
}  // namespace bcos::evm::state::test
```

- [ ] **Step 2: Write the suite scaffold with the helper-contract sanity test**

Create `bcos-evm/test/state/SparseStorageOverlayTest.cpp`:

```cpp
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
```

- [ ] **Step 3: Wire into CMake**

Append to the END of `bcos-evm/test/cmake/StateTests.cmake` (follow the file's existing block pattern):

```cmake
set(SPARSE_STORAGE_OVERLAY_TEST_BINARY_NAME SparseStorageOverlayTest)

add_executable(${SPARSE_STORAGE_OVERLAY_TEST_BINARY_NAME}
    state/SparseStorageOverlayTest.cpp
)

target_include_directories(${SPARSE_STORAGE_OVERLAY_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${SPARSE_STORAGE_OVERLAY_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
)

add_test(
    NAME SparseStorageOverlay
    COMMAND ${SPARSE_STORAGE_OVERLAY_TEST_BINARY_NAME}
)
```

- [ ] **Step 4: Reconfigure, build and run the sanity test**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake .                                                   # register the newly added target
cmake --build . --target SparseStorageOverlayTest -j8
./bcos-evm/test/SparseStorageOverlayTest
```

The explicit `cmake .` is REQUIRED: with the Unix Makefiles generator a brand-new
`add_executable` has no forwarding rule yet, so `cmake --build . --target …` fails with
"No rule to make target" before any auto-reconfigure can run. (Subsequent builds of the
target in later tasks don't need it.)

Expected: reconfigure + build succeed; test output ends with `*** No errors detected`.

- [ ] **Step 5: Commit**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
git add bcos-evm/test/helpers/SparseStorageStateView.h \
        bcos-evm/test/state/SparseStorageOverlayTest.cpp \
        bcos-evm/test/cmake/StateTests.cmake
git commit -m "test(evm): sparse-storage StateView helper + overlay suite scaffold"
```

(If the clang-format pre-commit hook reformats and fails the commit: `git add` the same three paths again and re-run the commit command.)

---

### Task 2: Bug-exposing scenarios (red) + 4-site core fix (green)

**Files:**
- Modify: `bcos-evm/test/state/SparseStorageOverlayTest.cpp` (append test cases before the closing namespace)
- Modify: `bcos-evm/eth/state/Account.hpp` (~line 44, after `selfDestructScheduled`)
- Modify: `bcos-evm/eth/state/State.cpp` — `get_storage` (~:191), `clear_storage` (~:450), `touchCreateDeploymentAccount` (~:771)

**Interfaces:**
- Consumes: Task 1's helpers (`makeBaseWithSlot`, `kContract`, `kSlotKey`, `kSlotValue`, `addr`, `b32`, `eq`); `State` public API: `set_balance(const evmc_address&, const bcos::u256&)`, `set_nonce(const evmc_address&, uint64_t)`, `set_code(const evmc_address&, bcos::bytes, evmc_bytes32)`, `set_storage(const evmc_address&, const evmc_bytes32&, const evmc_bytes32&)`, `set_transient_storage(const evmc_address&, const evmc_bytes32&, const evmc_bytes32&)`, `mark_self_destructed(const evmc_address&)`, `touchOverlayAccount(const evmc_address&)`, `touchCreateDeploymentAccount(const evmc_address&, evmc_revision)`, `checkpoint()/commit()/revert()`, `get_storage(...) const`, `build_diff() const` → `StateDiff{ .accounts: map<evmc_address, Account> }`.
- Produces: `Account::storageReset` (`bool`, default `false`) — consumed by Tasks 3 and 4.

- [ ] **Step 1: Append the eleven bug-exposing tests**

(Scenario numbers interleave by category — [B] = 1-9,12,13 here; [G] = 10,11 in Task 3 —
so numbering is non-contiguous within each file section. Each test's name is descriptive.)

Append to `bcos-evm/test/state/SparseStorageOverlayTest.cpp`, immediately before the final `}  // namespace bcos::evm::state::test`:

```cpp
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
```

- [ ] **Step 2: Run — verify all eleven FAIL (and the sanity test still passes)**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target SparseStorageOverlayTest -j8
./bcos-evm/test/SparseStorageOverlayTest
```

Expected: **11 failures** — scenarios 1,4,5,6,7,8,12,13 fail their `eq(..., kSlotValue)` check (get 0); scenario 2 fails its `BOOST_REQUIRE(slotIt != ...)`; scenarios 3 and 9 fail their absent-slot check (a spurious slot IS exported). If any of the eleven PASSES pre-fix, STOP — the scenario is not exercising the bug; re-check against the spec before touching production code.

- [ ] **Step 3: Add the `storageReset` flag to `Account`**

In `bcos-evm/eth/state/Account.hpp`, find:

```cpp
    bool selfDestructed{false};
    /// Sticky tx-end deletion (evmone `destructed`): survives CREATE recreate touch.
    bool selfDestructScheduled{false};
```

Replace with:

```cpp
    bool selfDestructed{false};
    /// Sticky tx-end deletion (evmone `destructed`): survives CREATE recreate touch.
    bool selfDestructScheduled{false};
    /// Storage namespace reset this tx (CREATE deployment / clear_storage): absent keys
    /// mean deleted-zero, do NOT fall back to base (geth: block-scoped stateObjectsDestruct
    /// suppression, applied here per reset account).
    bool storageReset{false};
```

- [ ] **Step 4: Fix `State::get_storage`**

In `bcos-evm/eth/state/State.cpp`, find:

```cpp
evmc_bytes32 State::get_storage(const evmc_address& address, const evmc_bytes32& key) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        if (auto storageIt = it->second.storage.find(key); storageIt != it->second.storage.end())
        {
            return storageIt->second;
        }
        // Overlay owns storage namespace: missing key ⇒ zero, do not read base.
        return evmc_bytes32{};
    }
    return m_baseStateView->get_storage(address, key);
}
```

Replace with:

```cpp
evmc_bytes32 State::get_storage(const evmc_address& address, const evmc_bytes32& key) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        if (auto storageIt = it->second.storage.find(key); storageIt != it->second.storage.end())
        {
            return storageIt->second;
        }
        // Reset accounts (CREATE deployment / clear_storage) own their namespace: absent
        // keys are deleted-zero. Every other overlay account is a copy-on-write
        // materialization whose storage map is NOT authoritative (LedgerStateView loads
        // accounts without storage) — read through to base, matching geth's originStorage.
        if (it->second.storageReset)
        {
            return evmc_bytes32{};
        }
        return m_baseStateView->get_storage(address, key);
    }
    return m_baseStateView->get_storage(address, key);
}
```

- [ ] **Step 5: Set the flag at the two reset sites**

In `bcos-evm/eth/state/State.cpp`, `touchCreateDeploymentAccount` — find:

```cpp
    account.storage.clear();
    // Cancel pending deletion for init/CALL visibility; selfDestructScheduled remains until
    // tx-end finalize (evmone deleted_accounts / build_diff export).
    account.selfDestructed = false;
```

Replace with:

```cpp
    account.storage.clear();
    // Fresh CREATE deployment owns its storage namespace from here on: absent keys are
    // deleted-zero, never the base view's residual slots.
    account.storageReset = true;
    // Cancel pending deletion for init/CALL visibility; selfDestructScheduled remains until
    // tx-end finalize (evmone deleted_accounts / build_diff export).
    account.selfDestructed = false;
```

In `clear_storage` — find:

```cpp
    for (auto const& key : keys)
    {
        set_storage(address, key, evmc_bytes32{});
    }
}
```

Replace with:

```cpp
    for (auto const& key : keys)
    {
        set_storage(address, key, evmc_bytes32{});
    }
    // Set AFTER the zero-write loop: the loop's set_storage calls must still read
    // pre-reset values so full-storage views journal/export explicit zeros. The flag
    // then covers keys a sparse base view could not enumerate above.
    account.storageReset = true;
}
```

(The `account` reference from the function's top stays valid — `std::unordered_map` never invalidates references, and no other account is inserted in between.)

- [ ] **Step 6: Run — all suite tests green**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target SparseStorageOverlayTest -j8
./bcos-evm/test/SparseStorageOverlayTest
```

Expected: `*** No errors detected` (12 tests: sanity + eleven [B]).

- [ ] **Step 7: Run the neighboring unit gates**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target StateJournalRevertTest StateBuildDiffTest EvmCallFrameTest -j8
ctest -R "StateJournalRevert|StateBuildDiff" --output-on-failure
./bcos-evm/test/EvmCallFrameTest
```

Expected: `100% tests passed`; `EvmCallFrameTest` prints `*** No errors detected`.

- [ ] **Step 8: Commit**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
git add bcos-evm/eth/state/Account.hpp \
        bcos-evm/eth/state/State.cpp \
        bcos-evm/test/state/SparseStorageOverlayTest.cpp
git commit -m "fix(evm): restore base read-through for materialized overlay accounts

State::get_storage treated every overlay account as owning its complete
storage namespace, but LedgerStateView materializes accounts with an
empty storage map — value transfers, TSTORE, 7702 nonce/code installs
and touches made subsequent SLOADs return 0 and swallowed
SSTORE-to-zero (invisible to all full-storage-view tests). Scope the
suppression to the two genuine namespace-reset sites via
Account::storageReset (geth stateObjectsDestruct analog); whole-Account
journal snapshots give revert-correctness for free."
```

(clang-format retry per Global Constraints applies.)

---

### Task 3: Guard scenarios (reset semantics stay pinned)

**Files:**
- Modify: `bcos-evm/test/state/SparseStorageOverlayTest.cpp` (append before the closing namespace)

**Interfaces:**
- Consumes: Task 1 helpers; `State::clear_storage(const evmc_address&)`; `Account::storageReset` from Task 2.
- Produces: nothing new (terminal test task).

- [ ] **Step 1: Append the two guard tests**

```cpp
// ── [G] guards: pass pre- and post-fix — pin the reset semantics the flag preserves ──

// Scenario 10: CREATE deployment resets the namespace — base slot must read 0.
BOOST_AUTO_TEST_CASE(create_deployment_reset_suppresses_base_read)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.touchCreateDeploymentAccount(kContract, EVMC_CANCUN);
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), evmc_bytes32{}));
}

// Scenario 11: clear_storage resets the namespace — base slot must read 0.
// NOTE: do NOT assert build_diff export here — under a sparse view clear_storage
// enumerates an empty base->storage (documented out-of-scope limitation).
BOOST_AUTO_TEST_CASE(clear_storage_reset_suppresses_base_read)
{
    auto const view = makeBaseWithSlot();
    State state(view);

    state.clear_storage(kContract);
    BOOST_CHECK(eq(state.get_storage(kContract, kSlotKey), evmc_bytes32{}));
}
```

- [ ] **Step 2: Run the suite**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target SparseStorageOverlayTest -j8
./bcos-evm/test/SparseStorageOverlayTest
```

Expected: `*** No errors detected` (14 tests).

- [ ] **Step 3: Commit**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
git add bcos-evm/test/state/SparseStorageOverlayTest.cpp
git commit -m "test(evm): pin storage-reset guard semantics"
```

(clang-format retry per Global Constraints applies.)

---

### Task 4: `hasNonEmptyStorage` honors `storageReset` (EIP-7610 consistency)

**Files:**
- Modify: `bcos-evm/eth/state/State.cpp` — `hasNonEmptyStorage` (~:166)
- Modify: `bcos-evm/test/state/SparseStorageOverlayTest.cpp` (append before the closing namespace)

**Interfaces:**
- Consumes: `Account::storageReset` (Task 2); `State::hasNonEmptyStorage(const evmc_address&) const` (public); `InMemoryStateView` (`bcos-evm/test/helpers/InMemoryStateView.h`) — the FULL-storage view is required here: the sparse view cannot express "base has enumerable storage", which is exactly what this predicate consults.
- Produces: nothing new (terminal task for the code change set).

- [ ] **Step 1: Write the failing test**

Append to `bcos-evm/test/state/SparseStorageOverlayTest.cpp` — add the include at the top of the file, right after `#include "helpers/SparseStorageStateView.h"`:

```cpp
#include "helpers/InMemoryStateView.h"
```

and the test before the closing namespace:

```cpp
// EIP-7610 consistency: a reset account's residual base storage must not count as
// non-empty. Uses the FULL-storage InMemoryStateView — the predicate enumerates
// base->get_account()->storage, which a sparse view leaves empty (that sparse-view
// false-negative is the documented residual gap, NOT closed by this change).
BOOST_AUTO_TEST_CASE(has_non_empty_storage_honors_storage_reset)
{
    InMemoryStateView view;
    Account seeded;
    seeded.balance = 100;
    seeded.storage[kSlotKey] = kSlotValue;
    view.insert_account(kContract, std::move(seeded));

    State state(view);
    BOOST_CHECK(state.hasNonEmptyStorage(kContract));  // pre-reset: base slot counts

    state.touchCreateDeploymentAccount(kContract, EVMC_CANCUN);
    BOOST_CHECK(!state.hasNonEmptyStorage(kContract));  // reset: namespace is empty
}
```

- [ ] **Step 2: Run — verify it fails**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target SparseStorageOverlayTest -j8
./bcos-evm/test/SparseStorageOverlayTest --run_test=has_non_empty_storage_honors_storage_reset
```

Expected: FAIL on the second check — the overlay storage map is empty after the reset, so the current code falls through to the base account and finds `kSlotKey`.

- [ ] **Step 3: Implement**

In `bcos-evm/eth/state/State.cpp`, find:

```cpp
/// EIP-7610: any non-zero storage slot on merged overlay+base view.
bool State::hasNonEmptyStorage(const evmc_address& address) const
{
    if (auto const* overlay = find_overlay_account(address))
    {
        if (!overlay->storage.empty())
        {
            return true;
        }
    }
    if (auto const account = m_baseStateView->get_account(address))
    {
        return !account->storage.empty();
    }
    return false;
}
```

Replace with:

```cpp
/// EIP-7610: any non-zero storage slot on merged overlay+base view. A reset account
/// (storageReset) owns its namespace — residual base slots no longer exist for it.
bool State::hasNonEmptyStorage(const evmc_address& address) const
{
    if (auto const* overlay = find_overlay_account(address))
    {
        if (!overlay->storage.empty())
        {
            return true;
        }
        if (overlay->storageReset)
        {
            return false;
        }
    }
    if (auto const account = m_baseStateView->get_account(address))
    {
        return !account->storage.empty();
    }
    return false;
}
```

- [ ] **Step 4: Run — suite fully green**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target SparseStorageOverlayTest -j8
./bcos-evm/test/SparseStorageOverlayTest
```

Expected: `*** No errors detected` (15 tests).

- [ ] **Step 5: Commit**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
git add bcos-evm/eth/state/State.cpp \
        bcos-evm/test/state/SparseStorageOverlayTest.cpp
git commit -m "fix(evm): hasNonEmptyStorage treats storage-reset accounts as empty"
```

(clang-format retry per Global Constraints applies.)

---

### Task 5: Full gate verification (fix must be inert under full-storage views)

**Files:** none (verification only).

**Interfaces:**
- Consumes: everything above.
- Produces: the go/no-go verdict. These gates cannot exercise the new branches (full-storage views make the fallback unreachable by construction) — green proves the fix changes nothing they cover; the new suite is the sole load-bearing coverage.

- [ ] **Step 1: Build the gate targets**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target bcos-evm-eth bcos-evm-op bcos-evm-bcos bcos-evm-storage \
    StateJournalRevertTest StateBuildDiffTest EvmCallFrameTest -j8
```

Expected: all `Built target …`, zero errors.

- [ ] **Step 2: Run unit gates**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
ctest -R "StateJournalRevert|StateBuildDiff|SparseStorageOverlay" --output-on-failure
./bcos-evm/test/EvmCallFrameTest
```

Expected: `100% tests passed, 0 tests failed out of 3`; `*** No errors detected`.

- [ ] **Step 3: Run the EEST / execution-spec smokes**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
ctest -R "EthEestStateGranularSmoke|EthEestBlockchainSmoke|EthEestBlockGranularSmoke" --output-on-failure
ctest -R "OpStackEestStateSmoke|OpStackEestBlockchainSmoke" --output-on-failure
ctest -R "EthExecutionSpec1153TstoreSmoke|EthExecutionSpec7702CoreSmoke|EthExecutionSpec6780Smoke" --output-on-failure
```

Expected: every invocation reports `100% tests passed, 0 tests failed`.
If a smoke fails: first check whether it fails on `HEAD~N` **before** this plan's commits (the concurrent work stream has had pre-existing failures, e.g. `Bcos6780SelfdestructTest`); only debug it here if this plan's commits introduced it.

- [ ] **Step 4: Report**

Summarize: tests added (15 in `SparseStorageOverlay`), production sites changed (Account.hpp flag; State.cpp get_storage / touchCreateDeploymentAccount / clear_storage / hasNonEmptyStorage), gates run and their results — honestly, including any failure and its pre-existing/new classification.
