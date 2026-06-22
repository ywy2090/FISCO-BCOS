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

#include <cstdint>

namespace bcos::evm
{

/// Infer typed tx kind when the RLP type byte is missing or legacy (0).
/// Priority: 0x04 auth → 0x03 blob → 0x02 EIP-1559 caps → 0x01 access list → legacy.
inline uint8_t inferWeb3TypedTxKindFromFields(bool authorizationListKeyPresent,
    bool hasAuthorizationList, bool hasBlobVersionedHashes, bool hasEip1559FeeCaps,
    bool hasAccessList) noexcept
{
    if (authorizationListKeyPresent || hasAuthorizationList)
    {
        return 0x04;
    }
    if (hasBlobVersionedHashes)
    {
        return 0x03;
    }
    if (hasEip1559FeeCaps)
    {
        return 0x02;
    }
    if (hasAccessList)
    {
        return 0x01;
    }
    return 0;
}

}  // namespace bcos::evm
