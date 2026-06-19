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
 *
 * @brief Execution-state journal with checkpoint/revert and EIP-2929 access tracking.
 * @file State.hpp
 */

#pragma once

#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include <optional>
#include <unordered_set>
#include <vector>

namespace bcos::evm::state
{
struct WarmStorageKeyHash
{
    size_t operator()(std::pair<evmc_address, evmc_bytes32> const& value) const noexcept
    {
        size_t hash = AddressHash{}(value.first);
        boost::hash_combine(hash, Bytes32Hash{}(value.second));
        return hash;
    }
};

struct WarmStorageKeyEqual
{
    bool operator()(std::pair<evmc_address, evmc_bytes32> const& lhs,
        std::pair<evmc_address, evmc_bytes32> const& rhs) const noexcept
    {
        return AddressEqual{}(lhs.first, rhs.first) && Bytes32Equal{}(lhs.second, rhs.second);
    }
};

class State : public StateView
{
public:
    explicit State(StateView const& baseStateView);

    [[nodiscard]] std::optional<Account> get_account(const evmc_address& address) const override;
    [[nodiscard]] bcos::u256 get_balance(const evmc_address& address) const override;
    [[nodiscard]] uint64_t get_nonce(const evmc_address& address) const override;
    [[nodiscard]] bcos::bytes get_code(const evmc_address& address) const override;
    [[nodiscard]] evmc_bytes32 get_code_hash(const evmc_address& address) const override;
    [[nodiscard]] evmc_bytes32 get_storage(
        const evmc_address& address, const evmc_bytes32& key) const override;
    [[nodiscard]] std::optional<Account> find(const evmc_address& address) const;

    void checkpoint();
    void revert();
    void commit();
    [[nodiscard]] bool has_checkpoint() const noexcept;

    void set_balance(const evmc_address& address, const bcos::u256& balance);
    void set_nonce(const evmc_address& address, uint64_t nonce);
    void set_code(const evmc_address& address, bcos::bytes code, evmc_bytes32 codeHash);
    void set_storage(
        const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value);
    void set_transient_storage(
        const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value);

    [[nodiscard]] bool warm_up_address(const evmc_address& address);
    [[nodiscard]] bool warm_up_storage(const evmc_address& address, const evmc_bytes32& key);
    [[nodiscard]] bool is_address_warm(const evmc_address& address) const;
    [[nodiscard]] bool is_storage_warm(const evmc_address& address, const evmc_bytes32& key) const;

    [[nodiscard]] StateDiff build_diff() const;

    void add_refund(uint64_t amount);
    [[nodiscard]] uint64_t get_refund() const noexcept;
    void clear_refund() noexcept;

private:
    enum class JournalType : uint8_t
    {
        AccountSnapshot,
        WarmAddressInsert,
        WarmStorageInsert
    };

    struct JournalEntry
    {
        JournalType type{JournalType::AccountSnapshot};
        evmc_address address{};
        evmc_bytes32 key{};
        std::optional<Account> previousAccount;
    };

    struct Checkpoint
    {
        size_t journalSize{0};
        uint64_t gasRefund{0};
        std::unordered_set<evmc_address, AddressHash, AddressEqual> touchedAccounts;
    };

    Account& mutable_account(const evmc_address& address);
    void journal_account_once(const evmc_address& address);
    void push_journal_account(const evmc_address& address, std::optional<Account> previous);
    void push_journal_warm_address(const evmc_address& address);
    void push_journal_warm_storage(const evmc_address& address, const evmc_bytes32& key);

private:
    StateView const* m_baseStateView;
    std::unordered_map<evmc_address, Account, AddressHash, AddressEqual> m_accounts;
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_warmAccounts;
    std::unordered_set<std::pair<evmc_address, evmc_bytes32>, WarmStorageKeyHash,
        WarmStorageKeyEqual>
        m_warmStorage;
    std::vector<JournalEntry> m_journal;
    std::vector<Checkpoint> m_checkpoints;
    uint64_t m_gasRefund{0};
};
}  // namespace bcos::evm::state
