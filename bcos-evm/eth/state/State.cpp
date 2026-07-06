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
 * @file State.cpp
 */

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"

#include "eth/state/StateDiff.hpp"
#include "eth/state/StateKeyHash.hpp"
#include "eth/state/StateView.hpp"
#include <algorithm>

namespace bcos::evm::state
{
State::State(StateView const& baseStateView) : m_baseStateView(&baseStateView) {}

std::optional<Account> State::find(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second;
    }
    return m_baseStateView->get_account(address);
}

std::optional<Account> State::get_account(const evmc_address& address) const
{
    return find(address);
}

bcos::u256 State::get_balance(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second.balance;
    }
    return m_baseStateView->get_balance(address);
}

uint64_t State::get_nonce(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second.nonce;
    }
    return m_baseStateView->get_nonce(address);
}

bcos::bytes State::get_code(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        if (!it->second.code.empty() || it->second.codeDirty)
        {
            return it->second.code;
        }
        return m_baseStateView->get_code(address);
    }
    return m_baseStateView->get_code(address);
}

size_t State::get_code_size(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        if (!it->second.code.empty() || it->second.codeDirty)
        {
            return it->second.code.size();
        }
        return m_baseStateView->get_code(address).size();
    }
    return m_baseStateView->get_code(address).size();
}

size_t State::copy_code(
    const evmc_address& address, size_t code_offset, uint8_t* buffer_data, size_t buffer_size) const
{
    bcos::bytesConstRef codeRef;
    bcos::bytes baseCode;
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        codeRef = bcos::bytesConstRef(it->second.code.data(), it->second.code.size());
    }
    else
    {
        baseCode = m_baseStateView->get_code(address);
        codeRef = bcos::bytesConstRef(baseCode.data(), baseCode.size());
    }

    if (code_offset >= codeRef.size() || buffer_size == 0)
    {
        return 0;
    }
    auto const count = std::min(buffer_size, codeRef.size() - code_offset);
    std::copy_n(codeRef.data() + code_offset, count, buffer_data);
    return count;
}

evmc_bytes32 State::get_code_hash(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        auto const& account = it->second;
        if (account.code.empty() && !account.codeDirty)
        {
            // Lazy-load from base when the account pre-existed; tx-created touch-only
            // overlay entries (value transfer, warm pin) have no base account but still
            // exist on state — EIP-1052 EXTCODEHASH must return keccak256(""), not 0.
            if (m_baseStateView->get_account(address).has_value())
            {
                return m_baseStateView->get_code_hash(address);
            }
            return emptyCodeHash();
        }
        if (account.code.empty())
        {
            return emptyCodeHash();
        }
        if (!isZeroBytes32(account.codeHash))
        {
            return account.codeHash;
        }
        return keccak256Code(bcos::bytesConstRef{account.code.data(), account.code.size()});
    }
    return m_baseStateView->get_code_hash(address);
}

bool State::account_exists(const evmc_address& address) const
{
    if (m_accounts.find(address) != m_accounts.end())
    {
        return true;
    }
    return m_baseStateView->get_account(address).has_value();
}

Account const* State::find_overlay_account(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return &it->second;
    }
    return nullptr;
}

evmc_bytes32 State::get_storage(const evmc_address& address, const evmc_bytes32& key) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        if (auto storageIt = it->second.storage.find(key); storageIt != it->second.storage.end())
        {
            return storageIt->second;
        }
        // Overlay account present: absent keys are zero (geth: dirty object owns storage).
        // Required after touchCreateDeploymentAccount / clear_storage — must not read base.
        return evmc_bytes32{};
    }
    return m_baseStateView->get_storage(address, key);
}

bool State::has_checkpoint() const noexcept
{
    return !m_checkpoints.empty();
}

void State::checkpoint()
{
    m_checkpoints.push_back({m_journal.size(), m_gasRefund, {}});
}

void State::commit()
{
    if (m_checkpoints.empty())
    {
        return;
    }

    if (m_checkpoints.size() >= 2)
    {
        auto top = std::move(m_checkpoints.back());
        m_checkpoints.pop_back();
        auto& parent = m_checkpoints.back();
        for (auto const& address : top.touchedAccounts)
        {
            parent.touchedAccounts.insert(address);
        }
        return;
    }

    m_checkpoints.pop_back();
}

void State::revert()
{
    if (m_checkpoints.empty())
    {
        return;
    }

    auto const checkpoint = std::move(m_checkpoints.back());
    m_checkpoints.pop_back();

    while (m_journal.size() > checkpoint.journalSize)
    {
        auto const entry = std::move(m_journal.back());
        m_journal.pop_back();
        switch (entry.type)
        {
        case JournalType::AccountSnapshot:
            if (entry.previousAccount.has_value())
            {
                m_accounts[entry.address] = *entry.previousAccount;
            }
            else
            {
                m_accounts.erase(entry.address);
            }
            break;
        case JournalType::WarmAddressInsert:
            if (!m_pinnedWarmAccounts.contains(entry.address))
            {
                m_warmAccounts.erase(entry.address);
            }
            break;
        case JournalType::WarmStorageInsert:
            m_warmStorage.erase({entry.address, entry.key});
            break;
        case JournalType::CreateWarmPinInsert:
            m_pinnedWarmAccounts.erase(entry.address);
            if (entry.pinInsertedWarm)
            {
                m_warmAccounts.erase(entry.address);
            }
            break;
        }
    }

    m_gasRefund = checkpoint.gasRefund;
}

Account& State::mutable_account(const evmc_address& address)
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second;
    }

    auto account = m_baseStateView->get_account(address);
    auto [it, _] = m_accounts.emplace(address, account.value_or(Account{}));
    return it->second;
}

void State::push_journal_account(const evmc_address& address, std::optional<Account> previous)
{
    m_journal.push_back(
        JournalEntry{JournalType::AccountSnapshot, address, evmc_bytes32{}, std::move(previous)});
}

void State::push_journal_warm_address(const evmc_address& address)
{
    m_journal.push_back(
        JournalEntry{JournalType::WarmAddressInsert, address, evmc_bytes32{}, std::nullopt});
}

void State::push_journal_warm_storage(const evmc_address& address, const evmc_bytes32& key)
{
    m_journal.push_back(JournalEntry{JournalType::WarmStorageInsert, address, key, std::nullopt});
}

void State::push_journal_create_warm_pin(const evmc_address& address, bool insertedWarm)
{
    JournalEntry entry{};
    entry.type = JournalType::CreateWarmPinInsert;
    entry.address = address;
    entry.pinInsertedWarm = insertedWarm;
    m_journal.push_back(std::move(entry));
}

void State::journal_account_once(const evmc_address& address)
{
    if (m_checkpoints.empty())
    {
        return;
    }

    auto& checkpoint = m_checkpoints.back();
    if (!checkpoint.touchedAccounts.insert(address).second)
    {
        return;
    }

    push_journal_account(address, find(address));
}

void State::set_balance(const evmc_address& address, const bcos::u256& balance)
{
    journal_account_once(address);
    auto& account = mutable_account(address);
    account.balance = balance;
    account.balanceDirty = true;
}

void State::set_nonce(const evmc_address& address, uint64_t nonce)
{
    journal_account_once(address);
    auto& account = mutable_account(address);
    account.nonce = nonce;
    account.nonceDirty = true;
}

void State::set_code(const evmc_address& address, bcos::bytes code, evmc_bytes32 codeHash)
{
    journal_account_once(address);
    auto& account = mutable_account(address);
    account.code = std::move(code);
    account.codeHash = codeHash;
    account.codeDirty = true;
}

void State::set_storage(
    const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value)
{
    auto const prior = get_storage(address, key);
    if (Bytes32Equal{}(prior, value))
    {
        return;
    }

    journal_account_once(address);
    mutable_account(address).storage[key] = value;
}

void State::clear_storage(const evmc_address& address)
{
    journal_account_once(address);
    mutable_account(address).storage.clear();
}

void State::set_transient_storage(
    const evmc_address& address, const evmc_bytes32& key, const evmc_bytes32& value)
{
    journal_account_once(address);
    mutable_account(address).transientStorage[key] = value;
}

evmc_bytes32 State::get_transient_storage(
    const evmc_address& address, const evmc_bytes32& key) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        if (auto storageIt = it->second.transientStorage.find(key);
            storageIt != it->second.transientStorage.end())
        {
            return storageIt->second;
        }
        return {};
    }
    if (auto const account = m_baseStateView->get_account(address); account.has_value())
    {
        if (auto storageIt = account->transientStorage.find(key);
            storageIt != account->transientStorage.end())
        {
            return storageIt->second;
        }
    }
    return {};
}

void State::clearAllTransientStorage()
{
    for (auto& [address, account] : m_accounts)
    {
        (void)address;
        account.transientStorage.clear();
    }
}

bool State::warm_up_address(const evmc_address& address)
{
    auto const inserted = m_warmAccounts.insert(address).second;
    if (WarmAccessProbe::enabled())
    {
        auto& probe = WarmAccessProbe::instance();
        if (inserted)
        {
            ++probe.warmUpCold;
        }
        else
        {
            ++probe.warmUpWarm;
        }
    }
    if (inserted && has_checkpoint())
    {
        push_journal_warm_address(address);
    }
    return inserted;
}

bool State::warm_up_storage(const evmc_address& address, const evmc_bytes32& key)
{
    auto const inserted = m_warmStorage.insert({address, key}).second;
    if (inserted && has_checkpoint())
    {
        push_journal_warm_storage(address, key);
    }
    return inserted;
}

bool State::warm_up_address_no_journal(const evmc_address& address)
{
    auto const inserted = m_warmAccounts.insert(address).second;
    if (WarmAccessProbe::enabled())
    {
        auto& probe = WarmAccessProbe::instance();
        if (inserted)
        {
            ++probe.warmUpNoJournalCold;
        }
        else
        {
            ++probe.warmUpNoJournalWarm;
        }
    }
    return inserted;
}

bool State::warm_up_storage_no_journal(const evmc_address& address, const evmc_bytes32& key)
{
    return m_warmStorage.insert({address, key}).second;
}

void State::journal_warm_address_for_revert(const evmc_address& address)
{
    if (!m_warmAccounts.contains(address) || !has_checkpoint())
    {
        return;
    }
    push_journal_warm_address(address);
}

void State::pin_create_pre_snapshot_warm(const evmc_address& address)
{
    (void)warm_up_address_no_journal(address);
    m_pinnedWarmAccounts.insert(address);
}

void State::unpin_create_pre_snapshot_warm(const evmc_address& address)
{
    m_pinnedWarmAccounts.erase(address);
}

void State::pin_warm_create_address(const evmc_address& address)
{
    bool const insertedWarm = m_warmAccounts.insert(address).second;
    if (WarmAccessProbe::enabled())
    {
        auto& probe = WarmAccessProbe::instance();
        if (insertedWarm)
        {
            ++probe.pinWarmNewInsert;
        }
        else
        {
            ++probe.pinWarmAlreadyWarm;
        }
    }
    m_pinnedWarmAccounts.insert(address);
    if (has_checkpoint())
    {
        push_journal_create_warm_pin(address, insertedWarm);
    }
}

bool State::is_address_warm(const evmc_address& address) const
{
    return m_warmAccounts.contains(address);
}

bool State::is_storage_warm(const evmc_address& address, const evmc_bytes32& key) const
{
    return m_warmStorage.contains({address, key});
}

StateDiff State::build_diff() const
{
    StateDiff diff;
    diff.accounts.reserve(m_accounts.size());
    for (auto const& [address, account] : m_accounts)
    {
        Account persisted = account;
        persisted.transientStorage.clear();
        diff.accounts.emplace(address, std::move(persisted));
    }
    return diff;
}

void State::mark_self_destructed(const evmc_address& address)
{
    journal_account_once(address);
    mutable_account(address).selfDestructed = true;
}

bool State::has_self_destructed(const evmc_address& address) const
{
    if (auto it = m_accounts.find(address); it != m_accounts.end())
    {
        return it->second.selfDestructed;
    }
    if (auto const account = m_baseStateView->get_account(address); account.has_value())
    {
        return account->selfDestructed;
    }
    return false;
}

void State::finalize_self_destructs()
{
    for (auto& [address, account] : m_accounts)
    {
        if (!account.selfDestructed)
        {
            continue;
        }
        account.code.clear();
        account.codeHash = {};
        account.storage.clear();
        account.balance = 0;
        account.nonce = 0;
        account.selfDestructed = false;
    }
}

void State::touchCreateDeploymentAccount(const evmc_address& address, evmc_revision revision)
{
    if (state::isZeroAddress(address))
    {
        return;
    }

    journal_account_once(address);
    auto& account = mutable_account(address);
    account.code.clear();
    account.codeHash = {};
    account.codeDirty = true;
    account.storage.clear();
    // Fresh CREATE target starts at nonce 1 (Spurious Dragon+). Same-tx collision recreate
    // (EIP-6780) must preserve an existing contract nonce — geth does not reset it on re-init.
    if (account.nonce == 0 && revision >= EVMC_SPURIOUS_DRAGON)
    {
        account.nonce = 1U;
    }
}

bool State::isPreexistingAccount(const evmc_address& address) const
{
    auto const account = m_baseStateView->get_account(address);
    if (!account.has_value())
    {
        return false;
    }
    return !account->code.empty() || account->nonce != 0;
}

void State::add_refund(uint64_t amount)
{
    m_gasRefund += amount;
}

void State::sub_refund(uint64_t amount)
{
    m_gasRefund = (m_gasRefund >= amount) ? (m_gasRefund - amount) : 0;
}

uint64_t State::get_refund() const noexcept
{
    return m_gasRefund;
}

void State::clear_refund() noexcept
{
    m_gasRefund = 0;
}
}  // namespace bcos::evm::state
