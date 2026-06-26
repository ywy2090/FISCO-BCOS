/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Infer Web3 typed transaction kind from tx fields (GST / TE fallback).
 * @file Web3TypedTxKind.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/Eip1559Access.h"
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm
{

/// EIP-2718 typed transaction envelope byte (0 = legacy RLP without type prefix).
enum class Web3TypedTxKind : uint8_t
{
    Legacy = 0x00,
    EIP2930 = 0x01,
    EIP1559 = 0x02,
    EIP4844 = 0x03,
    EIP7702 = 0x04,
};

inline constexpr uint8_t toWeb3TypedTxKindValue(Web3TypedTxKind kind) noexcept
{
    return static_cast<uint8_t>(kind);
}

/// Infer typed tx kind when the RLP type byte is missing or legacy (0).
/// Priority: 0x04 auth → 0x03 blob → 0x02 EIP-1559 caps → 0x01 access list → legacy.
inline uint8_t inferWeb3TypedTxKindFromFields(bool authorizationListKeyPresent,
    bool hasAuthorizationList, bool hasBlobVersionedHashes, bool hasEip1559FeeCaps,
    bool hasAccessList) noexcept
{
    if (authorizationListKeyPresent || hasAuthorizationList)
    {
        return toWeb3TypedTxKindValue(Web3TypedTxKind::EIP7702);
    }
    if (hasBlobVersionedHashes)
    {
        return toWeb3TypedTxKindValue(Web3TypedTxKind::EIP4844);
    }
    if (hasEip1559FeeCaps)
    {
        return toWeb3TypedTxKindValue(Web3TypedTxKind::EIP1559);
    }
    if (hasAccessList)
    {
        return toWeb3TypedTxKindValue(Web3TypedTxKind::EIP2930);
    }
    return toWeb3TypedTxKindValue(Web3TypedTxKind::Legacy);
}

/// Reject EIP-2718 typed txs on forks that do not support them (geth/Besu parity).
inline bool isTypedTxKindSupportedByRevision(
    uint8_t web3TypedTxKind, bcos::evm_standard::RevisionConfig const& revision) noexcept
{
    switch (web3TypedTxKind)
    {
    case toWeb3TypedTxKindValue(Web3TypedTxKind::Legacy):
        return true;
    case toWeb3TypedTxKindValue(Web3TypedTxKind::EIP2930):
        return revision.revision >= EVMC_BERLIN;
    case toWeb3TypedTxKindValue(Web3TypedTxKind::EIP1559):
        return gas::isEip1559TypedTxAllowed(revision);
    case toWeb3TypedTxKindValue(Web3TypedTxKind::EIP4844):
        return revision.eip4844;
    case toWeb3TypedTxKindValue(Web3TypedTxKind::EIP7702):
        return revision.eip7702;
    default:
        return false;
    }
}

}  // namespace bcos::evm
