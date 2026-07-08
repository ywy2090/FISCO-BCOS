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
