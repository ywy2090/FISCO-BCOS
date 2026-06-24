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
 * @brief Minimal bloom filter for transition receipt skeleton.
 * @file BloomFilter.hpp
 */

#pragma once

#include <evmc/evmc.h>
#include <array>

namespace bcos::evm::state
{
class BloomFilter
{
public:
    static constexpr size_t BLOOM_BYTES = 256;

    void clear() noexcept { m_bits.fill(0); }

    void add(const evmc_bytes32& value) noexcept;
    void merge(const BloomFilter& other) noexcept;

private:
    std::array<uint8_t, BLOOM_BYTES> m_bits{};
};
}  // namespace bcos::evm::state
