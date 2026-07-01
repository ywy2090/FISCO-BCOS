/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack blob tx intent and versioned-hash precheck helpers.
 * @file OpStackBlobTxChecks.h
 */

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
