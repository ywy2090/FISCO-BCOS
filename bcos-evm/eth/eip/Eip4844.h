/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief EIP-4844 blob gas helpers (Cancun+).
 * @file Eip4844.h
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm::gas
{

constexpr uint64_t BLOB_GAS_PER_BLOB = 131'072;
constexpr uint64_t MIN_BLOB_GAS_PRICE = 1;
constexpr uint64_t BLOB_GASPRICE_UPDATE_FRACTION = 3'338'477;
/// Cancun per-tx blob cap (matches blobSchedule.max = 6 / geth MaxBlobsPerBlock).
constexpr uint64_t MAX_BLOBS_PER_TX = 6;

inline bool hasBlobTxIntent(
    uint8_t web3TypedTxKind, bool hasBlobVersionedHashes, bool hasMaxFeePerBlobGas) noexcept
{
    return web3TypedTxKind == 0x03 || hasBlobVersionedHashes || hasMaxFeePerBlobGas;
}

// geth consensus/misc/eip4844.fakeExponential — factor * e^(numerator / denominator).
inline bcos::u256 fakeExponential(
    bcos::u256 factor, bcos::u256 numerator, bcos::u256 denominator) noexcept
{
    bcos::u256 output{};
    bcos::u256 accum = factor * denominator;
    for (uint64_t i = 1; accum > 0; ++i)
    {
        output += accum;
        accum = (accum * numerator) / denominator / bcos::u256(i);
    }
    return output / denominator;
}

inline bcos::u256 calcBlobBaseFee(uint64_t excessBlobGas) noexcept
{
    return fakeExponential(bcos::u256{MIN_BLOB_GAS_PRICE}, bcos::u256{excessBlobGas},
        bcos::u256{BLOB_GASPRICE_UPDATE_FRACTION});
}

}  // namespace bcos::evm::gas
