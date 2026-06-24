#pragma once
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include <bcos-utilities/FixedBytes.h>

namespace bcos::evm
{
inline bool hasBlobTxIntent(OpStackExecutionRequest const& input) noexcept
{
    return input.web3TypedTxKind == 0x03 || !input.blobVersionedHashes.empty();
}

inline bool isValidVersionedHash(bcos::h256 const& h) noexcept
{
    return h[0] == 0x01;
}
}  // namespace bcos::evm
