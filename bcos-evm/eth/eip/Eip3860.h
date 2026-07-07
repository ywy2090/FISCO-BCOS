#pragma once

#include <evmc/evmc.h>
#include <cstddef>

namespace bcos::evm
{

inline constexpr size_t MAX_INIT_CODE_SIZE = 49'152;

/// EIP-3860: reject contract-creating txs whose initcode exceeds the fork limit.
/// eip3860 comes from RevisionConfig.eip3860 (single source of truth for fork gating).
inline bool isInitCodeSizeExceeded(bool eip3860, evmc_call_kind kind, size_t inputSize) noexcept
{
    if (!eip3860 || kind != EVMC_CREATE)
    {
        return false;
    }
    return inputSize > MAX_INIT_CODE_SIZE;
}

}  // namespace bcos::evm
