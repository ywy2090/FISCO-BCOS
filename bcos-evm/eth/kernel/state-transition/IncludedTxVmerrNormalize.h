#pragma once

#include "bcos-evm/eth/kernel/EVMCResult.h"

namespace bcos::evm
{

inline bool isTopLevelIncludedTxVmError(evmc_status_code status, int32_t depth) noexcept
{
    if (depth != 0)
    {
        return false;
    }

    switch (status)
    {
    case EVMC_SUCCESS:
    case EVMC_REVERT:
    case EVMC_INSUFFICIENT_BALANCE:
    case EVMC_INTERNAL_ERROR:
        return false;
    default:
        return true;
    }
}

inline void normalizeIncludedTxVmerr(EVMCResult& result, int32_t depth) noexcept
{
    if (!isTopLevelIncludedTxVmError(result.status_code, depth))
    {
        return;
    }
    result.status_code = EVMC_SUCCESS;
    result.status = protocol::TransactionStatus::None;
}

// EIP-7702: delegation indicators persist when execution reverts; the transaction is still
// included with success status (state root reflects applied authorizations).
inline void normalizeSetCodeTransactionVmerr(
    EVMCResult& result, int32_t depth, bool authorizationListPresent) noexcept
{
    if (depth != 0 || !authorizationListPresent || result.status_code != EVMC_REVERT)
    {
        return;
    }
    result.status_code = EVMC_SUCCESS;
    result.status = protocol::TransactionStatus::None;
}

}  // namespace bcos::evm
