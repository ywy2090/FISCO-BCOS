/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Apply execution StateDiff to production ledger storage (EVMAccount).
 * @file StateDiffApplier.h
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-task/Task.h"
#include <string>
#include <string_view>

namespace bcos::evm::state
{
template <class Storage>
task::Task<void> applyStateDiff(Storage& storage, const StateDiff& diff, bool useBinaryAddress,
    const bcos::crypto::Hash& hashImpl, std::string_view defaultAbi = {})
{
    for (auto const& [address, accountDiff] : diff.accounts)
    {
        ledger::account::EVMAccount account(storage, address, useBinaryAddress);
        if (!co_await account.exists())
        {
            co_await account.create();
        }

        if (accountDiff.balanceDirty)
        {
            co_await account.setBalance(accountDiff.balance);
        }
        if (accountDiff.nonceDirty)
        {
            co_await account.setNonce(bcos::u256(accountDiff.nonce).str());
        }

        // Write code on any codeDirty diff, including an empty-code write: an EIP-7702
        // delegation revocation (authorization to 0x0) clears the authority's code, and
        // dropping that write would leave the stale 0xef0100||addr designator in storage
        // so the "revoked" delegate keeps executing.
        if (accountDiff.codeDirty)
        {
            auto const codeHash =
                hashImpl.hash(bytesConstRef(accountDiff.code.data(), accountDiff.code.size()));
            std::string abi;
            if (auto abiEntry = co_await account.abi(); abiEntry.has_value())
            {
                abi = std::string(abiEntry->get());
            }
            else
            {
                abi = std::string(defaultAbi);
            }
            co_await account.setCode(accountDiff.code, std::move(abi), codeHash);
        }

        for (auto const& [key, value] : accountDiff.storage)
        {
            co_await account.setStorage(key, value);
        }
    }

    // Pre-EIP-6780 SELFDESTRUCT deletions. The overlay entry was erased (so it is absent
    // from diff.accounts), leaving the ledger account with its pre-tx balance/code — which
    // duplicates the ETH already credited to the beneficiary and keeps the contract callable.
    // Zero it out to match the destroyed empty-account state (post-EIP158 hashing strips
    // empty accounts, so a zeroed account and an absent one hash identically).
    // NOTE: EVMAccount exposes no row-removal or storage-key enumeration, so residual storage
    // slots cannot be cleared here; a full table-removal primitive is needed for exact parity
    // on chains that both run pre-Cancun AND re-CREATE at a self-destructed address.
    for (auto const& address : diff.deletedAccounts)
    {
        ledger::account::EVMAccount account(storage, address, useBinaryAddress);
        if (!co_await account.exists())
        {
            continue;
        }
        co_await account.setBalance(bcos::u256{0});
        co_await account.setNonce(bcos::u256{0}.str());
        co_await account.setCode({}, std::string(defaultAbi), hashImpl.hash(bytesConstRef{}));
    }
}
}  // namespace bcos::evm::state
