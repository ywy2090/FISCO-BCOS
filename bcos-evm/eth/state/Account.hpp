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
 * @brief In-memory account model for execution-time State journal.
 * @file Account.hpp
 */

#pragma once

#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <boost/container_hash/hash.hpp>
#include <cstring>
#include <unordered_map>

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

using StorageMap = std::unordered_map<evmc_bytes32, evmc_bytes32, Bytes32Hash, Bytes32Equal>;

struct Account
{
    bcos::u256 balance{0};
    uint64_t nonce{0};
    bcos::bytes code;
    evmc_bytes32 codeHash{};
    StorageMap storage;
    StorageMap transientStorage;
};
}  // namespace bcos::evm::state
