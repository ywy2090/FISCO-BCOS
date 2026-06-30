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
 * @file BloomFilter.cpp
 */

#include "helpers/BloomFilter.hpp"

namespace bcos::evm::state
{
namespace
{
constexpr size_t BLOOM_BITS = BloomFilter::BLOOM_BYTES * 8;

size_t bloomBitFromWord(uint16_t word) noexcept
{
    return static_cast<size_t>(word & 0x07ff);  // 2048 bits
}
}  // namespace

void BloomFilter::add(const evmc_bytes32& value) noexcept
{
    // Skeleton bloom mapping: take 3x16-bit windows from the 32-byte payload.
    for (size_t i = 0; i < 3; ++i)
    {
        auto const offset = i * 2;
        auto const word =
            static_cast<uint16_t>((value.bytes[offset] << 8) | value.bytes[offset + 1]);
        auto const bit = bloomBitFromWord(word);
        auto const byteIndex = (BLOOM_BITS - 1 - bit) / 8;
        auto const bitInByte = bit % 8;
        m_bits[byteIndex] = static_cast<uint8_t>(m_bits[byteIndex] | (1U << bitInByte));
    }
}

void BloomFilter::merge(const BloomFilter& other) noexcept
{
    for (size_t i = 0; i < BLOOM_BYTES; ++i)
    {
        m_bits[i] = static_cast<uint8_t>(m_bits[i] | other.m_bits[i]);
    }
}
}  // namespace bcos::evm::state
