/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Production storage read adapter: StateView backed by ledger::EVMAccount.
 * @file LedgerStateView.h
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-task/Wait.h"
#include <functional>
#include <optional>

namespace bcos::evm::state
{
class LedgerStateView : public StateView
{
public:
    template <class Storage>
    LedgerStateView(Storage& storage, bool useBinaryAddress, const bcos::crypto::Hash& hashImpl)
      : m_storageRead([&storage, useBinaryAddress](
                          const evmc_address& address, const evmc_bytes32& key) -> evmc_bytes32 {
            ledger::account::EVMAccount account(storage, address, useBinaryAddress);
            if (!task::syncWait(account.exists()))
            {
                return {};
            }
            return task::syncWait(account.storage(key));
        }),
        m_accountRead([&storage, useBinaryAddress, &hashImpl](
                          const evmc_address& address) -> std::optional<Account> {
            ledger::account::EVMAccount account(storage, address, useBinaryAddress);
            if (!task::syncWait(account.exists()))
            {
                return std::nullopt;
            }

            Account loadedAccount;
            loadedAccount.balance = task::syncWait(account.balance());

            if (auto nonce = task::syncWait(account.nonce()); nonce.has_value() && !nonce->empty())
            {
                loadedAccount.nonce = bcos::u256(*nonce).convert_to<uint64_t>();
            }

            if (auto codeEntry = task::syncWait(account.code()); codeEntry.has_value())
            {
                auto const codeView = codeEntry->get();
                loadedAccount.code.assign(codeView.begin(), codeView.end());
            }

            auto const codeHash = task::syncWait(account.codeHash());
            loadedAccount.codeHash = state::toEvmC(codeHash);

            // Legacy contracts may store code in account.code without codeHash.
            if (isZeroHash(loadedAccount.codeHash) && !loadedAccount.code.empty())
            {
                loadedAccount.codeHash = state::toEvmC(hashImpl.hash(
                    bytesConstRef(loadedAccount.code.data(), loadedAccount.code.size())));
            }

            return loadedAccount;
        }),
        m_existsRead([&storage, useBinaryAddress](const evmc_address& address) -> bool {
            ledger::account::EVMAccount account(storage, address, useBinaryAddress);
            return task::syncWait(account.exists());
        }),
        m_balanceRead([&storage, useBinaryAddress](const evmc_address& address) -> bcos::u256 {
            ledger::account::EVMAccount account(storage, address, useBinaryAddress);
            if (!task::syncWait(account.exists()))
            {
                return bcos::u256{0};
            }
            return task::syncWait(account.balance());
        }),
        m_nonceRead([&storage, useBinaryAddress](const evmc_address& address) -> uint64_t {
            ledger::account::EVMAccount account(storage, address, useBinaryAddress);
            if (!task::syncWait(account.exists()))
            {
                return 0;
            }
            if (auto nonce = task::syncWait(account.nonce()); nonce.has_value() && !nonce->empty())
            {
                return bcos::u256(*nonce).convert_to<uint64_t>();
            }
            return 0;
        }),
        m_codeRead([&storage, useBinaryAddress](const evmc_address& address) -> bcos::bytes {
            ledger::account::EVMAccount account(storage, address, useBinaryAddress);
            if (!task::syncWait(account.exists()))
            {
                return {};
            }
            bcos::bytes code;
            if (auto codeEntry = task::syncWait(account.code()); codeEntry.has_value())
            {
                auto const codeView = codeEntry->get();
                code.assign(codeView.begin(), codeView.end());
            }
            return code;
        }),
        m_codeHashRead([&storage, useBinaryAddress, &hashImpl](
                           const evmc_address& address) -> evmc_bytes32 {
            ledger::account::EVMAccount account(storage, address, useBinaryAddress);
            if (!task::syncWait(account.exists()))
            {
                return {};
            }
            // Mirrors StateView::get_code_hash default semantics exactly:
            // empty code → emptyCodeHash regardless of any stored hash; legacy
            // contracts without a stored codeHash hash the code on the fly.
            bcos::bytes code;
            if (auto codeEntry = task::syncWait(account.code()); codeEntry.has_value())
            {
                auto const codeView = codeEntry->get();
                code.assign(codeView.begin(), codeView.end());
            }
            if (code.empty())
            {
                return emptyCodeHash();
            }
            auto codeHash = state::toEvmC(task::syncWait(account.codeHash()));
            if (isZeroHash(codeHash))
            {
                codeHash = state::toEvmC(hashImpl.hash(bytesConstRef(code.data(), code.size())));
            }
            return codeHash;
        })
    {}

    std::optional<Account> get_account(const evmc_address& address) const override;
    evmc_bytes32 get_storage(const evmc_address& address, const evmc_bytes32& key) const override;

    // Narrow per-field reads. Without these, every base-view field read (BALANCE,
    // EXTCODESIZE/HASH, CALL code load, existence probes) fell through to the StateView
    // defaults, which call get_account — a five-read full account load (exists + balance
    // + nonce + code + codeHash) per lookup.
    bool account_exists(const evmc_address& address) const override;
    bcos::u256 get_balance(const evmc_address& address) const override;
    uint64_t get_nonce(const evmc_address& address) const override;
    bcos::bytes get_code(const evmc_address& address) const override;
    evmc_bytes32 get_code_hash(const evmc_address& address) const override;

private:
    static bool isZeroHash(const evmc_bytes32& value);

    std::function<evmc_bytes32(const evmc_address&, const evmc_bytes32&)> m_storageRead;
    std::function<std::optional<Account>(const evmc_address&)> m_accountRead;
    std::function<bool(const evmc_address&)> m_existsRead;
    std::function<bcos::u256(const evmc_address&)> m_balanceRead;
    std::function<uint64_t(const evmc_address&)> m_nonceRead;
    std::function<bcos::bytes(const evmc_address&)> m_codeRead;
    std::function<evmc_bytes32(const evmc_address&)> m_codeHashRead;
};
}  // namespace bcos::evm::state
