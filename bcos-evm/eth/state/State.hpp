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
 *
 * @brief Mutable execution-time state overlay with journal, checkpoint/revert, and EIP-2929
 *        warm/cold access tracking.
 * @file State.hpp
 *
 * ## Role in the execution stack
 *
 * State is the **write-through overlay** used during EVM message execution. It extends the
 * read-only StateView interface with in-memory account mutations, gas-refund accounting,
 * transient storage (EIP-1153), and access-list warm/cold tracking (EIP-2929).
 *
 * Persistent ledger storage is **not** touched here. After successful execution, callers
 * snapshot overlay changes via build_diff() into a StateDiff, which Transaction Executor
 * applies to ledger storage through applyStateDiff().
 *
 * ## Layering model
 *
 *   LedgerStateView / TestStateView  (cold base, read-only)
 *              │
 *              ▼
 *         State (overlay: m_accounts)
 *              │
 *              ▼
 *         build_diff() → StateDiff → applyStateDiff() → EVMAccount
 *
 * Reads merge overlay first, then fall back to the base StateView. Writes always go through
 * mutable_account(), which copies a base account into m_accounts on first touch.
 *
 * ## Checkpoint / journal / revert
 *
 * EVM nested calls use checkpoint() at frame entry and either commit() or revert() at exit:
 *
 *   checkpoint()  — push a Checkpoint { journal cursor, gas refund, touched set }
 *   commit()      — pop checkpoint; merge touchedAccounts into parent (nested) or discard
 *                   top-level checkpoint entirely
 *   revert()      — unwind journal entries back to checkpoint cursor; restore gas refund
 *
 * Account mutations journal a single AccountSnapshot per address per checkpoint (via
 * journal_account_once). Warm-access inserts are journaled separately so REVERT restores
 * cold/warm sets correctly (geth parity).
 *
 * ## Dirty flags on Account
 *
 * balanceDirty / nonceDirty / codeDirty gate which fields applyStateDiff() writes to ledger.
 * Storage slots are applied whenever present in the diff map (no per-slot dirty bit).
 * Transient storage lives only in overlay memory and is stripped by build_diff().
 *
 * @see StateView.hpp, StateDiff.hpp, StateDiffApplier.h, Account.hpp
 */

#pragma once

#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/StateKeyHash.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-evm/eth/state/WarmAccessProbe.h"
#include <optional>
#include <unordered_set>
#include <vector>

namespace bcos::evm::state
{

/// Execution-time mutable state overlay. See file header for architecture and lifecycle.
class State : public StateView
{
public:
    /// @param baseStateView Immutable cold state (ledger or test fixture); must outlive State.
    explicit State(StateView const& baseStateView);

    // ── Read path (overlay → base fallback) ──────────────────────────────────

    [[nodiscard]] std::optional<Account> get_account(const evmc_address& address) const override;
    [[nodiscard]] bcos::u256 get_balance(const evmc_address& address) const override;
    [[nodiscard]] uint64_t get_nonce(const evmc_address& address) const override;
    [[nodiscard]] bcos::bytes get_code(const evmc_address& address) const override;
    [[nodiscard]] size_t get_code_size(const evmc_address& address) const;
    [[nodiscard]] size_t copy_code(const evmc_address& address, size_t code_offset,
        uint8_t* buffer_data, size_t buffer_size) const;
    [[nodiscard]] evmc_bytes32 get_code_hash(const evmc_address& address) const override;
    [[nodiscard]] evmc_bytes32 get_storage(
        const evmc_address& address, const evmc_bytes32& key) const override;
    [[nodiscard]] bool account_exists(const evmc_address& address) const override;
    /// EIP-7610: true when any storage slot exists on the merged account view.
    [[nodiscard]] bool hasNonEmptyStorage(const evmc_address& address) const;
    /// Overlay-first merge; used by journal_account_once to capture pre-mutation snapshot.
    [[nodiscard]] std::optional<Account> find(const evmc_address& address) const;
    /// Overlay-only lookup; nullptr when address has not been touched this execution.
    [[nodiscard]] Account const* find_overlay_account(const evmc_address& address) const;

    // ── Checkpoint / journal (EVM call-frame nesting) ────────────────────────

    /// Push a revert boundary; subsequent mutations are journaled until commit/revert.
    void checkpoint();
    /// Undo all journal entries and warm-access changes since the innermost checkpoint().
    void revert();
    /// Accept changes since checkpoint(); nested frames merge touchedAccounts into parent.
    void commit();
    [[nodiscard]] bool has_checkpoint() const noexcept;
    /// Open checkpoint count; lets noexcept host boundaries rebalance the stack after a throw.
    [[nodiscard]] size_t checkpoint_depth() const noexcept;

    // ── Account mutations (each sets the corresponding Account::*Dirty flag) ─

    void set_balance(const evmc_address& address, const bcos::u256& balance);
    /// Credit @p delta wei to @p address (no-op when @p delta is zero).
    void add_balance(const evmc_address& address, const bcos::u256& delta);
    /// Transfer @p value wei from @p from to @p to. Returns false when @p from is
    /// underfunded; leaves state unchanged on failure.
    [[nodiscard]] bool transfer_balance(
        const evmc_address& from, const evmc_address& to, const bcos::u256& value);
    void set_nonce(const evmc_address& address, uint64_t nonce);
    void set_code(const evmc_address& address, bcos::bytes code, evmc_bytes32 codeHash);
    void set_storage(
        const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value);
    void clear_storage(const evmc_address& address);

    // ── Transient storage (EIP-1153; tx-scoped, never persisted) ─────────────

    void set_transient_storage(
        const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value);
    [[nodiscard]] evmc_bytes32 get_transient_storage(
        const evmc_address& address, const evmc_bytes32& key) const;
    /// Clears in-memory transient slots for all overlay accounts (end-of-tx hygiene).
    void clearAllTransientStorage();

    // ── EIP-2929 warm/cold access tracking ───────────────────────────────────

    [[nodiscard]] bool warm_up_address(const evmc_address& address);
    [[nodiscard]] bool warm_up_storage(const evmc_address& address, const evmc_bytes32& key);
    [[nodiscard]] bool warm_up_address_no_journal(const evmc_address& address);
    [[nodiscard]] bool warm_up_storage_no_journal(
        const evmc_address& address, const evmc_bytes32& key);
    void pin_warm_create_address(const evmc_address& address);
    /// Record pre-snapshot CREATE warm for journal revert when this frame commits to a parent
    /// that may still REVERT (geth: access-list changes roll back with the enclosing call).
    void journal_warm_address_for_revert(const evmc_address& address);
    /// Re-establish pre-snapshot CREATE warm after frame revert (survives WarmAddressInsert undo).
    void pin_create_pre_snapshot_warm(const evmc_address& address);
    void unpin_create_pre_snapshot_warm(const evmc_address& address);
    [[nodiscard]] bool is_address_warm(const evmc_address& address) const;
    [[nodiscard]] bool is_storage_warm(const evmc_address& address, const evmc_bytes32& key) const;

    // ── Diff output (post-execution persistence candidate) ───────────────────

    /// Snapshot overlay mutations into StateDiff; only exports fields/slots that differ
    /// from tx-start committed state (storage uses first-SSTORE original; see evmone).
    [[nodiscard]] StateDiff build_diff() const;

    // ── SELFDESTRUCT (EIP-6780 semantics via host + finalize) ──────────────

    void mark_self_destructed(const evmc_address& address);
    [[nodiscard]] bool has_self_destructed(const evmc_address& address) const;
    [[nodiscard]] bool is_self_destruct_scheduled(const evmc_address& address) const;
    /// End-of-tx: legacy paths erase self-destructed overlay accounts; EIP-6780 zeroes fields.
    void finalize_self_destructs(bool eip6780 = false);

    // ── CREATE / CREATE2 deployment prep ─────────────────────────────────────

    /// Clear prior deployment residue before CREATE/CREATE2 init (EIP-6780 same-tx recreate).
    void touchCreateDeploymentAccount(const evmc_address& address, evmc_revision revision);

    /// Materialize an empty overlay account (pre-Byzantium CALL new-account touch).
    void touchOverlayAccount(const evmc_address& address);

    /// True when address had a non-empty pre-tx account in base state (geth: !IsNewContract).
    [[nodiscard]] bool isPreexistingAccount(const evmc_address& address) const;

    // ── Gas refund counter (checkpoint-scoped via journal revert) ────────────

    void add_refund(uint64_t amount);
    void sub_refund(uint64_t amount);
    [[nodiscard]] uint64_t get_refund() const noexcept;
    void clear_refund() noexcept;

private:
    /// Discriminant for revert(): each type restores a different facet of execution state.
    enum class JournalType : uint8_t
    {
        AccountSnapshot,      ///< Restore or erase m_accounts[address] from previousAccount
        WarmAddressInsert,    ///< Remove address from m_warmAccounts (unless pinned)
        WarmStorageInsert,    ///< Remove (address,key) from m_warmStorage
        CreateWarmPinInsert,  ///< Undo pin_warm_create_address side effects
    };

    struct JournalEntry
    {
        JournalType type{JournalType::AccountSnapshot};
        evmc_address address{};
        evmc_bytes32 key{};
        std::optional<Account> previousAccount;
        /// True when pin_warm_create_address inserted into m_warmAccounts.
        bool pinInsertedWarm{false};
    };

    /// Saved at checkpoint(); revert() rewinds m_journal to journalSize and restores gasRefund.
    struct Checkpoint
    {
        size_t journalSize{0};
        uint64_t gasRefund{0};
        /// Addresses already snapshotted in this frame; prevents duplicate journal entries.
        std::unordered_set<evmc_address, AddressHash, AddressEqual> touchedAccounts;
    };

    /// Ensure overlay entry exists (copy-on-first-write from base).
    Account& mutable_account(const evmc_address& address);
    /// Tx-start storage value before the first SSTORE to (address, key) in this execution.
    void ensureStorageOriginal(const evmc_address& address, const evmc_bytes32& key);
    [[nodiscard]] evmc_bytes32 storageOriginalAtTxStart(
        const evmc_address& address, const evmc_bytes32& key) const;
    /// Record pre-mutation account snapshot once per address per checkpoint.
    void journal_account_once(const evmc_address& address);
    void push_journal_account(const evmc_address& address, std::optional<Account> previous);
    void push_journal_warm_address(const evmc_address& address);
    void push_journal_warm_storage(const evmc_address& address, const evmc_bytes32& key);
    void push_journal_create_warm_pin(const evmc_address& address, bool insertedWarm);

    StateView const* m_baseStateView;
    std::unordered_map<evmc_address, Account, AddressHash, AddressEqual> m_accounts;
    /// Populated by finalize_self_destructs (legacy erase path) for build_diff export.
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_deletedAccounts;
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_warmAccounts;
    /// CREATE warm pins survive WarmAddressInsert revert until explicitly unpinned.
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_pinnedWarmAccounts;
    std::unordered_set<std::pair<evmc_address, evmc_bytes32>, WarmStorageKeyHash,
        WarmStorageKeyEqual>
        m_warmStorage;
    std::vector<JournalEntry> m_journal;
    std::vector<Checkpoint> m_checkpoints;
    uint64_t m_gasRefund{0};
    /// Committed storage at first SSTORE per (address, slot) in this tx (evmone original).
    std::unordered_map<std::pair<evmc_address, evmc_bytes32>, evmc_bytes32, WarmStorageKeyHash,
        WarmStorageKeyEqual>
        m_storageOriginal;
};

/// Post-CREATE helper: copy successful initcode output into overlay code (Host callback path).
inline void installCreatedContractCode(
    State& state, const evmc_message& message, const evmc_result& result)
{
    if (result.status_code != EVMC_SUCCESS || result.output_size == 0 ||
        result.output_data == nullptr)
    {
        return;
    }
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
    {
        return;
    }

    auto createAddr = message.recipient;
    // EVMC may leave recipient zero; fall back through code_address and create_address.
    if (isZeroAddress(createAddr))
    {
        createAddr = message.code_address;
    }
    if (isZeroAddress(createAddr))
    {
        createAddr = result.create_address;
    }
    if (isZeroAddress(createAddr))
    {
        return;
    }
    // geth: skip code store when initcode SELFDESTRUCTed the create target in this frame.
    if (state.has_self_destructed(createAddr))
    {
        return;
    }

    bcos::bytes code(result.output_data, result.output_data + result.output_size);
    state.set_code(createAddr, std::move(code), {});
}
}  // namespace bcos::evm::state
