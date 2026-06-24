#include "bcos-evm/bcos/FiscoEvmStateReader.h"

namespace bcos::evm::state
{
std::optional<Account> FiscoEvmStateReader::get_account(const evmc_address& address) const
{
    return m_accountRead(address);
}

evmc_bytes32 FiscoEvmStateReader::get_storage(
    const evmc_address& address, const evmc_bytes32& key) const
{
    return m_storageRead(address, key);
}

bool FiscoEvmStateReader::isZeroHash(const evmc_bytes32& value)
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
