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

        if (accountDiff.codeDirty && !accountDiff.code.empty())
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
}
}  // namespace bcos::evm::state
