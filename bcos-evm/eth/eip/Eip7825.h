#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include <cstdint>

namespace bcos::evm
{

/// EIP-7825 (Osaka+): per-transaction gas limit cap is 2^24.
inline constexpr int64_t MAX_TX_GAS = 16'777'216;

/// Reject when tx gas limit strictly exceeds the Osaka cap (at-maximum is allowed).
inline bool isTxGasLimitExceeded(RevisionConfig const& cfg, int64_t gasLimit) noexcept
{
    return cfg.eip7825 && gasLimit > MAX_TX_GAS;
}

}  // namespace bcos::evm
