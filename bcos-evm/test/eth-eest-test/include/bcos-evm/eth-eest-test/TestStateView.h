#pragma once

#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include <unordered_map>

namespace bcos::evm::reference_tests
{

class TestStateView : public state::StateView
{
public:
    void insertAccount(evmc_address const& address, state::Account account = {})
    {
        m_accounts[address] = std::move(account);
    }

    std::optional<state::Account> get_account(evmc_address const& address) const override
    {
        auto const it = m_accounts.find(address);
        if (it == m_accounts.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std::unordered_map<evmc_address, state::Account, state::AddressHash, state::AddressEqual>
        m_accounts;
};

}  // namespace bcos::evm::reference_tests
