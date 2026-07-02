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
 * @brief Lightweight hash/equality functors for evmc address and storage keys.
 * @file StateKeyHash.hpp
 */

#pragma once

#include <evmc/evmc.h>
#include <boost/container_hash/hash.hpp>
#include <algorithm>
#include <cstring>
#include <utility>

namespace bcos::evm::state
{
struct AddressHash
{
    size_t operator()(evmc_address const& address) const noexcept
    {
        return boost::hash_range(address.bytes, address.bytes + sizeof(address.bytes));
    }
};

struct AddressEqual
{
    bool operator()(evmc_address const& lhs, evmc_address const& rhs) const noexcept
    {
        return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
    }
};

struct Bytes32Hash
{
    size_t operator()(evmc_bytes32 const& value) const noexcept
    {
        return boost::hash_range(value.bytes, value.bytes + sizeof(value.bytes));
    }
};

struct Bytes32Equal
{
    bool operator()(evmc_bytes32 const& lhs, evmc_bytes32 const& rhs) const noexcept
    {
        return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
    }
};

struct WarmStorageKeyHash
{
    size_t operator()(std::pair<evmc_address, evmc_bytes32> const& value) const noexcept
    {
        size_t hash = AddressHash{}(value.first);
        boost::hash_combine(hash, Bytes32Hash{}(value.second));
        return hash;
    }
};

struct WarmStorageKeyEqual
{
    bool operator()(std::pair<evmc_address, evmc_bytes32> const& lhs,
        std::pair<evmc_address, evmc_bytes32> const& rhs) const noexcept
    {
        return AddressEqual{}(lhs.first, rhs.first) && Bytes32Equal{}(lhs.second, rhs.second);
    }
};

inline bool isZeroAddress(const evmc_address& address) noexcept
{
    return std::all_of(std::begin(address.bytes), std::end(address.bytes),
        [](uint8_t value) { return value == 0; });
}

inline bool isZeroBytes32(const evmc_bytes32& value) noexcept
{
    return std::all_of(
        std::begin(value.bytes), std::end(value.bytes), [](uint8_t item) { return item == 0; });
}
}  // namespace bcos::evm::state
