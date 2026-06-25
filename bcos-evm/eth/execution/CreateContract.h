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
 * @brief CREATE / CREATE2 contract deployment helpers (geth evm.create parity).
 * @file CreateContract.h
 */

#pragma once

#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include <evmone_precompiles/keccak.hpp>

namespace bcos::evm::execution
{
constexpr size_t MAX_EVM_CODE_SIZE = 0x6000;
constexpr int64_t CREATE_DATA_GAS_PER_BYTE = 200;

inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

inline evmc_address predictCreate2Address(
    evmc_address const& sender, evmc_bytes32 const& salt, bcos::bytesConstRef initCode) noexcept
{
    bcos::bytes buffer;
    buffer.reserve(1 + sizeof(sender.bytes) + sizeof(salt.bytes) + 32);
    buffer.push_back(0xff);
    buffer.insert(buffer.end(), sender.bytes, sender.bytes + sizeof(sender.bytes));
    buffer.insert(buffer.end(), salt.bytes, salt.bytes + sizeof(salt.bytes));
    auto const initHash = ethash::keccak256(initCode.data(), initCode.size());
    buffer.insert(buffer.end(), initHash.bytes, initHash.bytes + sizeof(initHash.bytes));
    auto const hash = ethash::keccak256(buffer.data(), buffer.size());
    evmc_address out{};
    std::memcpy(out.bytes, hash.bytes + 12, sizeof(out.bytes));
    return out;
}

inline evmc_address predictCreateAddress(
    state::State& state, evmc_message const& message, bcos::bytesConstRef initCode) noexcept
{
    if (!state::isZeroAddress(message.recipient))
    {
        return message.recipient;
    }
    if (!state::isZeroAddress(message.code_address))
    {
        return message.code_address;
    }
    if (message.kind == EVMC_CREATE2)
    {
        return predictCreate2Address(message.sender, message.create2_salt, initCode);
    }
    return state::predictLegacyCreateAddress(message.sender, state.get_nonce(message.sender));
}

/// Bind CREATE recipient/code_address from sender nonce (no state mutation).
inline void bindCreateMessageForInit(state::EthHost& host, evmc_message& message,
    bcos::bytesConstRef initCode, state::State& state) noexcept
{
    if (!isCreateKind(message.kind))
    {
        return;
    }

    auto const createAddr = predictCreateAddress(state, message, initCode);
    if (state::isZeroAddress(createAddr))
    {
        return;
    }

    message.recipient = createAddr;
    message.code_address = createAddr;
    host.set_execution_address(createAddr);
}

/// Initialize CREATE target account (nonce=1, warm pin). Must run inside a checkpoint.
inline void initializeCreateTargetAccount(state::State& state, evmc_address const& createAddr,
    evmc_revision revision, bool warmAccess) noexcept
{
    if (state::isZeroAddress(createAddr))
    {
        return;
    }

    if (warmAccess)
    {
        state.pin_warm_create_address(createAddr);
    }

    if (revision >= EVMC_SPURIOUS_DRAGON)
    {
        state.set_nonce(createAddr, 1);
    }
}

/// geth CreateContract: set recipient/code_address, warm, and nonce=1 before initcode runs.
inline void prepareCreateTargetBeforeInit(state::State& state, state::EthHost& host,
    evmc_message& message, evmc_revision revision, bcos::bytesConstRef initCode,
    bool warmAccess) noexcept
{
    bindCreateMessageForInit(host, message, initCode, state);
    initializeCreateTargetAccount(state, message.recipient, revision, warmAccess);
}

/// Charge CREATE runtime-code deposit (200 gas/byte). Matches evmone test/state/host.cpp create().
/// @return true when deployment may proceed; false when initcode succeeded but deploy must fail.
inline bool applyCreateCodeDepositGas(evmc_result& result, evmc_revision revision) noexcept
{
    if (result.status_code != EVMC_SUCCESS)
    {
        return false;
    }
    if (result.output_size == 0 || result.output_data == nullptr)
    {
        return true;
    }

    auto const codeSize = static_cast<int64_t>(result.output_size);
    if (revision >= EVMC_SPURIOUS_DRAGON && codeSize > static_cast<int64_t>(MAX_EVM_CODE_SIZE))
    {
        result.status_code = EVMC_FAILURE;
        result.gas_left = 0;
        return false;
    }
    if (revision >= EVMC_LONDON && result.output_data[0] == 0xEF)
    {
        result.status_code = EVMC_CONTRACT_VALIDATION_FAILURE;
        result.gas_left = 0;
        return false;
    }

    auto const cost = codeSize * CREATE_DATA_GAS_PER_BYTE;
    if (result.gas_left < cost)
    {
        if (revision == EVMC_FRONTIER)
        {
            return true;
        }
        result.status_code = EVMC_FAILURE;
        result.gas_left = 0;
        return false;
    }
    result.gas_left -= cost;
    return true;
}
}  // namespace bcos::evm::execution
