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
 * @brief Standard Ethereum logs bloom (keccak256 → 3×11-bit indices).
 * @file BloomFilter.hpp
 */

#pragma once

#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <array>
#include <cstring>
#include <evmone_precompiles/keccak.hpp>
#include <span>

namespace bcos::evm::state
{
constexpr size_t LOGS_BLOOM_BYTES = 256;

/// Byte-wise OR aggregate for block-level logs bloom.
inline void bloomOr(bcos::bytes& acc, bcos::bytes const& other)
{
    if (acc.size() < LOGS_BLOOM_BYTES)
        acc.assign(LOGS_BLOOM_BYTES, 0);
    auto const n = std::min(other.size(), LOGS_BLOOM_BYTES);
    for (size_t i = 0; i < n; ++i)
        acc[i] = static_cast<bcos::byte>(acc[i] | other[i]);
}

namespace detail
{
constexpr uint8_t LOWER_3_BITS = 0b00000111;

inline void bytesToBloom(bcos::bytes& bloom, bcos::bytesConstRef data)
{
    auto const hash = ethash::keccak256(data.data(), data.size());
    for (size_t i = 0; i < 6; i += 2)
    {
        auto const bitPosition =
            static_cast<uint16_t>((hash.bytes[i] & LOWER_3_BITS) << 8) + hash.bytes[i + 1];
        auto const positionInBytes = LOGS_BLOOM_BYTES - 1 - (bitPosition / 8);
        bloom[positionInBytes] =
            static_cast<bcos::byte>(bloom[positionInBytes] | (1U << (bitPosition % 8)));
    }
}
}  // namespace detail

/// Per-transaction logs bloom (address + each topic, Yellow Paper / EIP-234).
inline bcos::bytes computeLogsBloom(std::span<LogEntry const> logs)
{
    bcos::bytes bloom(LOGS_BLOOM_BYTES, 0);
    for (auto const& log : logs)
    {
        detail::bytesToBloom(bloom, {log.address.bytes, sizeof(log.address.bytes)});
        for (auto const& topic : log.topics)
            detail::bytesToBloom(bloom, {topic.bytes, sizeof(topic.bytes)});
    }
    return bloom;
}

class BloomFilter
{
public:
    static constexpr size_t BLOOM_BYTES = LOGS_BLOOM_BYTES;

    void clear() noexcept { m_bits.fill(0); }

    void add(bcos::bytesConstRef value) noexcept
    {
        bcos::bytes bloom(BLOOM_BYTES, 0);
        detail::bytesToBloom(bloom, value);
        for (size_t i = 0; i < BLOOM_BYTES; ++i)
            m_bits[i] = static_cast<uint8_t>(m_bits[i] | bloom[i]);
    }

    void add(const evmc_bytes32& value) noexcept { add({value.bytes, sizeof(value.bytes)}); }

    void merge(const BloomFilter& other) noexcept
    {
        for (size_t i = 0; i < BLOOM_BYTES; ++i)
            m_bits[i] = static_cast<uint8_t>(m_bits[i] | other.m_bits[i]);
    }

    bcos::bytes toBytes() const { return bcos::bytes(m_bits.begin(), m_bits.end()); }

private:
    std::array<uint8_t, BLOOM_BYTES> m_bits{};
};
}  // namespace bcos::evm::state
