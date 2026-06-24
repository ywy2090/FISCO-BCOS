#pragma once

#include "bcos-evm/eth/EVMCResult.h"

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

}  // namespace bcos::evm
