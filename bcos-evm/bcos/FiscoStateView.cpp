#include "bcos-evm/bcos/FiscoStateView.h"

namespace bcos::evm::state
{
std::optional<Account> FiscoStateView::get_account(const evmc_address& address) const
{
    return m_accountRead(address);
}

evmc_bytes32 FiscoStateView::get_storage(const evmc_address& address, const evmc_bytes32& key) const
{
    return m_storageRead(address, key);
}

bool FiscoStateView::isZeroHash(const evmc_bytes32& value)
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
}  // namespace bcos::evm::state
