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
 * @brief CREATE deployment lifecycle helpers (init binding, account touch, code deposit).
 * @file CreateDeployment.h
 *
 * CREATE/CREATE2 address prediction lives in CreateAddress.h.
 */

#pragma once

#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-evm/eth/kernel/execution/CreateAddress.h"
#include "bcos-evm/eth/state/State.hpp"
#include <limits>
#include <optional>

namespace bcos::evm::execution
{
constexpr size_t MAX_EVM_CODE_SIZE = gas::MAX_CODE_SIZE_EIP170;
constexpr int64_t CREATE_DATA_GAS_PER_BYTE = gas::CODE_DEPOSIT_GAS_PER_BYTE;

/// Assign CREATE recipient/code_address from sender nonce (no state mutation; geth: create() addr).
inline void assignCreateAddresses(evmc_address& executionAddress, evmc_message& message,
    bcos::bytesConstRef initCode, state::State& st) noexcept
{
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
    {
        return;
    }

    auto const createAddr = predictCreateAddress(st, message, initCode);
    if (state::isZeroAddress(createAddr))
    {
        return;
    }

    message.recipient = createAddr;
    message.code_address = createAddr;
    executionAddress = createAddr;
}

/// Initialize CREATE target account (nonce=1). Must run inside a checkpoint.
/// EIP-2929 warm for the predicted address is applied before the frame checkpoint (geth: before Snapshot).
inline void initializeCreateTargetAccount(state::State& st, evmc_address const& createAddr,
    evmc_revision revision) noexcept
{
    if (state::isZeroAddress(createAddr))
    {
        return;
    }

    st.touchCreateDeploymentAccount(createAddr, revision);
}

/// EIP-6780: block CREATE/CREATE2 redeploy at an address already marked for same-tx deletion.
inline bool isSameTxSelfDestructedCreateBlocked(
    state::State const& st, evmc_address const& createAddr, bool eip6780) noexcept
{
    if (!eip6780 || state::isZeroAddress(createAddr))
    {
        return false;
    }
    return st.has_self_destructed(createAddr);
}

/// geth create/create2: fail when target already has nonce or code (address collision).
inline bool isCreateDeploymentAddressCollision(
    state::State const& st, evmc_address const& createAddr) noexcept
{
    if (state::isZeroAddress(createAddr) || !st.account_exists(createAddr))
    {
        return false;
    }
    return st.get_nonce(createAddr) != 0 || st.get_code_size(createAddr) != 0;
}

/// EIP-2681: sender nonce at uint64 max cannot increment for CREATE/CREATE2.
inline bool isCreateSenderNonceOverflow(
    state::State const& st, evmc_address const& sender) noexcept
{
    if (state::isZeroAddress(sender))
    {
        return false;
    }
    return st.get_nonce(sender) == std::numeric_limits<uint64_t>::max();
}

/// Assign addresses and nonce=1 before initcode runs (geth: create account touch).
inline void setupCreateTarget(state::State& st, evmc_address& executionAddress,
    evmc_message& message, evmc_revision revision, bcos::bytesConstRef initCode) noexcept
{
    assignCreateAddresses(executionAddress, message, initCode, st);
    initializeCreateTargetAccount(st, message.recipient, revision);
}

/// Post-init CREATE rejection reason (geth create() err classification).
enum class CreateCodeDepositReject : uint8_t
{
    MaxCodeSizeExceeded,
    InvalidCodePrefix,
    DepositOutOfGas,
};

/// Charge CREATE runtime-code deposit (200 gas/byte). Matches geth create() post-RETURN checks.
/// @return empty when deployment may proceed; otherwise the rejection kind (result.status updated).
inline std::optional<CreateCodeDepositReject> applyCreateCodeDepositGas(
    evmc_result& result, evmc_revision revision) noexcept
{
    if (result.status_code != EVMC_SUCCESS)
    {
        return std::nullopt;
    }
    if (result.output_size == 0 || result.output_data == nullptr)
    {
        return std::nullopt;
    }

    auto const codeSize = static_cast<int64_t>(result.output_size);
    if (revision >= EVMC_SPURIOUS_DRAGON && codeSize > static_cast<int64_t>(MAX_EVM_CODE_SIZE))
    {
        result.status_code = EVMC_FAILURE;
        result.gas_left = 0;
        return CreateCodeDepositReject::MaxCodeSizeExceeded;
    }
    if (revision >= EVMC_LONDON && result.output_data[0] == 0xEF)
    {
        result.status_code = EVMC_CONTRACT_VALIDATION_FAILURE;
        result.gas_left = 0;
        return CreateCodeDepositReject::InvalidCodePrefix;
    }

    auto const cost = codeSize * CREATE_DATA_GAS_PER_BYTE;
    if (result.gas_left < cost)
    {
        if (revision == EVMC_FRONTIER)
        {
            return std::nullopt;
        }
        result.status_code = EVMC_FAILURE;
        result.gas_left = 0;
        return CreateCodeDepositReject::DepositOutOfGas;
    }
    result.gas_left -= cost;
    return std::nullopt;
}
}  // namespace bcos::evm::execution
