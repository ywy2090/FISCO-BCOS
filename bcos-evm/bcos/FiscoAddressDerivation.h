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
 *
 * @brief Single module for FISCO CREATE / CREATE2 address derivation.
 * @file FiscoAddressDerivation.h
 */

#pragma once

#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.h>
#include <fmt/compile.h>
#include <fmt/format.h>
#include <array>
#include <cstring>

namespace bcos::evm
{

struct FiscoTopLevelCreateParams
{
    bool web3Tx{false};
    bool featureEvmAddress{false};
    protocol::BlockNumber blockNumber{0};
    int64_t contextID{0};
    int64_t seq{0};
    bcos::u256 txNonce{0};
    bcos::crypto::Hash const* hashImpl{nullptr};
};

struct FiscoNestedCreateParams
{
    bool web3Tx{false};
    bool featureEvmAddress{false};
    evmc_address callerAddress{};
    evmc_address origin{};
    protocol::BlockNumber blockNumber{0};
    int64_t contextID{0};
    int64_t* nestedSeq{nullptr};
    state::State const* state{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};
};

inline bool isEmptyEvmAddress(evmc_address const& address) noexcept
{
    return std::memcmp(address.bytes, EMPTY_EVM_ADDRESS.bytes, sizeof(address.bytes)) == 0;
}

inline evmc_address fiscoHashContractAddress(
    bcos::crypto::Hash const& hashImpl, int64_t blockNumber, int64_t contextID, int64_t seq)
{
    auto const label = fmt::format(FMT_COMPILE("{}_{}_{}"), blockNumber, contextID, seq);
    auto const digest = hashImpl.hash(label);
    evmc_address out{};
    std::copy_n(digest.data(), sizeof(out.bytes), out.bytes);
    return out;
}

inline evmc_address legacyCreateAddress(evmc_address const& deployer, bcos::u256 const& nonce)
{
    auto const raw =
        newLegacyEVMAddress(bcos::bytesConstRef(deployer.bytes, sizeof(deployer.bytes)), nonce);
    evmc_address out{};
    std::copy(raw.begin(), raw.end(), out.bytes);
    return out;
}

inline evmc_address predictFiscoCreate2Address(bcos::crypto::Hash const& hashImpl,
    evmc_address const& deployer, evmc_bytes32 const& salt, bcos::bytesConstRef initCode)
{
    std::array<bcos::byte,
        1 + sizeof(deployer.bytes) + sizeof(salt.bytes) + bcos::crypto::HashType::SIZE>
        buffer{};
    uint8_t* ptr = buffer.data();
    *ptr++ = 0xff;
    ptr = std::uninitialized_copy_n(deployer.bytes, sizeof(deployer.bytes), ptr);
    auto const saltBE = toBigEndian(state::fromEvmC(salt));
    ptr = std::uninitialized_copy(saltBE.begin(), saltBE.end(), ptr);
    auto const inputHash = hashImpl.hash(initCode);
    ptr = std::uninitialized_copy(inputHash.begin(), inputHash.end(), ptr);
    auto const addressHash = hashImpl.hash(bcos::bytesConstRef(buffer.data(), buffer.size()));
    evmc_address out{};
    std::copy_n(addressHash.begin() + 12, sizeof(out.bytes), out.bytes);
    return out;
}

inline evmc_address resolveCreateDeployer(
    evmc_address const& callerAddress, evmc_address const& sender) noexcept
{
    return !state::isZeroAddress(callerAddress) ? callerAddress : sender;
}

inline bool shouldSkipNestedCreateDerivation(
    evmc_message const& message, evmc_address const& origin) noexcept
{
    bool const contractSender =
        std::memcmp(message.sender.bytes, origin.bytes, sizeof(message.sender.bytes)) != 0;
    return message.depth == 0 && !contractSender;
}

inline bool useLegacyCreateAddress(bool web3Tx, bool featureEvmAddress) noexcept
{
    return web3Tx || featureEvmAddress;
}

inline void bindCreateAddressFields(evmc_message& message, evmc_address const& address)
{
    message.code_address = address;
    message.recipient = address;
}

/// Top-level orchestration path. Legacy when web3Tx || featureEvmAddress.
inline void applyTopLevelCreateDerivation(
    evmc_message& message, FiscoTopLevelCreateParams const& params)
{
    if (params.hashImpl == nullptr)
    {
        return;
    }

    switch (message.kind)
    {
    case EVMC_CREATE:
    {
        if (!isEmptyEvmAddress(message.code_address))
        {
            message.recipient = message.code_address;
            break;
        }
        if (useLegacyCreateAddress(params.web3Tx, params.featureEvmAddress))
        {
            bindCreateAddressFields(message, legacyCreateAddress(message.sender, params.txNonce));
        }
        else
        {
            bindCreateAddressFields(message, fiscoHashContractAddress(*params.hashImpl,
                                                 params.blockNumber, params.contextID, params.seq));
        }
        break;
    }
    case EVMC_CREATE2:
    {
        bindCreateAddressFields(message,
            predictFiscoCreate2Address(*params.hashImpl, message.sender, message.create2_salt,
                bcos::bytesConstRef(message.input_data, message.input_size)));
        break;
    }
    default:
        break;
    }
}

/// Nested EvmHostHooks path. Caller must skip via shouldSkipNestedCreateDerivation.
inline void applyNestedCreateDerivation(
    evmc_message& message, FiscoNestedCreateParams const& params)
{
    if (params.state == nullptr || params.hashImpl == nullptr)
    {
        return;
    }

    if (message.kind == EVMC_CREATE2)
    {
        auto const deployer = resolveCreateDeployer(params.callerAddress, message.sender);
        bindCreateAddressFields(
            message, predictFiscoCreate2Address(*params.hashImpl, deployer, message.create2_salt,
                         bcos::bytesConstRef(message.input_data, message.input_size)));
        return;
    }

    if (message.kind != EVMC_CREATE)
    {
        return;
    }

    if (useLegacyCreateAddress(params.web3Tx, params.featureEvmAddress))
    {
        auto const deployer = resolveCreateDeployer(params.callerAddress, message.sender);
        auto const nonce = params.state->get_nonce(deployer);
        bindCreateAddressFields(message, legacyCreateAddress(deployer, bcos::u256{nonce}));
    }
    else
    {
        int64_t const callSeq = params.nestedSeq != nullptr ? (++*params.nestedSeq) : 0;
        bindCreateAddressFields(message, fiscoHashContractAddress(*params.hashImpl,
                                             params.blockNumber, params.contextID, callSeq));
    }
}

}  // namespace bcos::evm
