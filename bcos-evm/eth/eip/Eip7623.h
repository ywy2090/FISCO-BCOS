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
 * @brief EIP-7623 calldata floor cost helpers (Prague+).
 * @file Eip7623.h
 */

#pragma once

#include "bcos-evm/eth/eip/ProtocolGas.h"
#include "bcos-utilities/Common.h"
#include <algorithm>
#include <limits>

namespace bcos::evm::gas
{

// token = 1 for zero byte, TOKENS_PER_NONZERO_BYTE for non-zero byte; floor = tokens * 10
inline constexpr int64_t TOKENS_PER_NONZERO_BYTE = 4;
inline constexpr int64_t TOTAL_COST_FLOOR_PER_TOKEN = 10;

struct Eip7623Components
{
    int64_t normalCost = 0;
    int64_t floorCost = 0;
    int64_t tokenCount = 0;
};

inline Eip7623Components calcEip7623Components(bcos::bytesConstRef data)
{
    constexpr auto maxSafeBytes =
        static_cast<size_t>(std::numeric_limits<int64_t>::max() /
                            (TOKENS_PER_NONZERO_BYTE * TOTAL_COST_FLOOR_PER_TOKEN));
    if (data.size() > maxSafeBytes)
    {
        return {std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
            std::numeric_limits<int64_t>::max()};
    }

    Eip7623Components components;
    for (auto byte : data)
    {
        if (byte == 0)
        {
            components.normalCost += ZERO_BYTE_INTRINSIC_COST;
            ++components.tokenCount;
        }
        else
        {
            components.normalCost += NONZERO_BYTE_INTRINSIC_COST;
            components.tokenCount += TOKENS_PER_NONZERO_BYTE;
        }
    }
    components.floorCost = components.tokenCount * TOTAL_COST_FLOOR_PER_TOKEN;
    return components;
}

inline int64_t calcEip7623CalldataGas(bcos::bytesConstRef data)
{
    auto const components = calcEip7623Components(data);
    return std::max(components.normalCost, components.floorCost);
}

}  // namespace bcos::evm::gas
