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

// fakeExponential for blob base fee from excess blob gas.
inline bcos::u256 calcBlobBaseFee(uint64_t excessBlobGas) noexcept
{
    bcos::u256 price{MIN_BLOB_GAS_PRICE};
    bcos::u256 numerator = price * bcos::u256(excessBlobGas);
    bcos::u256 denominator = bcos::u256(1) << 16;
    bcos::u256 output{};

    while (excessBlobGas > 0)
    {
        output += numerator / denominator;
        numerator /= denominator;
        if (numerator == 0)
        {
            break;
        }
        excessBlobGas >>= 16;
        numerator *= bcos::u256(excessBlobGas);
    }
    return output + price;
}

}  // namespace bcos::evm::gas
