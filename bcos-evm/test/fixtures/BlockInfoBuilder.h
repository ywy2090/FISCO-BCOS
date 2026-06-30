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
 * @brief Fluent builder for eth::state::BlockInfo in tests/fixtures.
 * @file BlockInfoBuilder.h
 */

#pragma once

#include "bcos-evm/eth/state/BlockInfo.hpp"

namespace bcos::evm::test
{
struct BlockInfoFields
{
    int64_t number{0};
    int64_t timestamp{0};
    int64_t gasLimit{0};
    evmc_address coinbase{};
    evmc_bytes32 prevRandao{};
    bcos::u256 baseFee{0};
    bcos::u256 chainId{0};
    bcos::u256 blobBaseFee{0};
    bcos::u256 gasPrice{0};
};

inline state::BlockInfo buildBlockInfo(const BlockInfoFields& fields)
{
    state::BlockInfo block;
    block.number = fields.number;
    block.timestamp = fields.timestamp;
    block.gasLimit = fields.gasLimit;
    block.coinbase = fields.coinbase;
    block.prevRandao = fields.prevRandao;
    block.baseFee = fields.baseFee;
    block.chainId = fields.chainId;
    block.blobBaseFee = fields.blobBaseFee;
    return block;
}

class BlockInfoBuilder
{
public:
    BlockInfoBuilder& number(int64_t value)
    {
        m_fields.number = value;
        return *this;
    }

    BlockInfoBuilder& timestamp(int64_t value)
    {
        m_fields.timestamp = value;
        return *this;
    }

    BlockInfoBuilder& gasLimit(int64_t value)
    {
        m_fields.gasLimit = value;
        return *this;
    }

    BlockInfoBuilder& coinbase(const evmc_address& value)
    {
        m_fields.coinbase = value;
        return *this;
    }

    BlockInfoBuilder& prevRandao(const evmc_bytes32& value)
    {
        m_fields.prevRandao = value;
        return *this;
    }

    BlockInfoBuilder& baseFee(const bcos::u256& value)
    {
        m_fields.baseFee = value;
        return *this;
    }

    BlockInfoBuilder& chainId(const bcos::u256& value)
    {
        m_fields.chainId = value;
        return *this;
    }

    BlockInfoBuilder& blobBaseFee(const bcos::u256& value)
    {
        m_fields.blobBaseFee = value;
        return *this;
    }

    BlockInfoBuilder& gasPrice(const bcos::u256& value)
    {
        m_fields.gasPrice = value;
        return *this;
    }

    state::BlockInfo build() const { return buildBlockInfo(m_fields); }

private:
    BlockInfoFields m_fields;
};
}  // namespace bcos::evm::test
