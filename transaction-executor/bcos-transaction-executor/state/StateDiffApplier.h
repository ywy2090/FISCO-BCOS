#pragma once

#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-task/Task.h"
#include <string_view>

namespace bcos::evm::state
{
template <class Storage>
task::Task<void> applyStateDiff(Storage& storage, const StateDiff& diff, bool useBinaryAddress,
    const bcos::crypto::Hash& hashImpl, std::string_view abi)
{
    for (auto const& [address, accountDiff] : diff.accounts)
    {
        ledger::account::EVMAccount account(storage, address, useBinaryAddress);
        if (!co_await account.exists())
        {
            co_await account.create();
        }

        co_await account.setBalance(accountDiff.balance);
        co_await account.setNonce(bcos::u256(accountDiff.nonce).str());

        if (!accountDiff.code.empty())
        {
            auto const codeHash =
                hashImpl.hash(bytesConstRef(accountDiff.code.data(), accountDiff.code.size()));
            co_await account.setCode(accountDiff.code, std::string(abi), codeHash);
        }

        for (auto const& [key, value] : accountDiff.storage)
        {
            co_await account.setStorage(key, value);
        }
    }
}
}  // namespace bcos::evm::state
