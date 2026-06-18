#pragma once

#include "../RollbackableStorage.h"
#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-task/Wait.h"
#include <optional>

namespace bcos::evm::state
{
class FiscoStateView : public StateView
{
public:
    template <class Storage>
    FiscoStateView(
        Rollbackable<Storage>& storage, bool useBinaryAddress, const bcos::crypto::Hash& hashImpl)
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
            loadedAccount.codeHash = toEvmC(codeHash);

            // Legacy contracts may store code in account.code without codeHash.
            if (isZeroHash(loadedAccount.codeHash) && !loadedAccount.code.empty())
            {
                loadedAccount.codeHash = toEvmC(hashImpl.hash(
                    bytesConstRef(loadedAccount.code.data(), loadedAccount.code.size())));
            }

            return loadedAccount;
        })
    {}

    std::optional<Account> get_account(const evmc_address& address) const override
    {
        return m_accountRead(address);
    }

    evmc_bytes32 get_storage(const evmc_address& address, const evmc_bytes32& key) const override
    {
        return m_storageRead(address, key);
    }

private:
    static bool isZeroHash(const evmc_bytes32& value)
    {
        for (auto byte : value.bytes)
        {
            if (byte != 0)
            {
                return false;
            }
        }
        return true;
    }

    std::function<evmc_bytes32(const evmc_address&, const evmc_bytes32&)> m_storageRead;
    std::function<std::optional<Account>(const evmc_address&)> m_accountRead;
};
}  // namespace bcos::evm::state
