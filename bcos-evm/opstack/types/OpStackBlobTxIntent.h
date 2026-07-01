#pragma once
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include <bcos-utilities/FixedBytes.h>

namespace bcos::evm
{
inline bool hasBlobTxIntent(OpStackMessageRequest const& input) noexcept
{
    return input.web3TypedTxKind == 0x03 || !input.blobVersionedHashes.empty();
}

inline bool isValidVersionedHash(bcos::h256 const& h) noexcept
{
    return h[0] == 0x01;
}
}  // namespace bcos::evm
