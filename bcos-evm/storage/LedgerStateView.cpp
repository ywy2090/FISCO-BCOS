#include "bcos-evm/storage/LedgerStateView.h"

namespace bcos::evm::state
{
std::optional<Account> LedgerStateView::get_account(const evmc_address& address) const
{
    return m_accountRead(address);
}

evmc_bytes32 LedgerStateView::get_storage(
    const evmc_address& address, const evmc_bytes32& key) const
{
    return m_storageRead(address, key);
}

bool LedgerStateView::account_exists(const evmc_address& address) const
{
    return m_existsRead(address);
}

bcos::u256 LedgerStateView::get_balance(const evmc_address& address) const
{
    return m_balanceRead(address);
}

uint64_t LedgerStateView::get_nonce(const evmc_address& address) const
{
    return m_nonceRead(address);
}

bcos::bytes LedgerStateView::get_code(const evmc_address& address) const
{
    return m_codeRead(address);
}

evmc_bytes32 LedgerStateView::get_code_hash(const evmc_address& address) const
{
    return m_codeHashRead(address);
}

bool LedgerStateView::isZeroHash(const evmc_bytes32& value)
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
