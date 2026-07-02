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

#include "bcos-evm/eth/kernel/CallKind.h"
#include "bcos-evm/eth/kernel/execution/CreateAddress.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{
constexpr size_t MAX_EVM_CODE_SIZE = 0x6000;
constexpr int64_t CREATE_DATA_GAS_PER_BYTE = 200;

/// Assign CREATE recipient/code_address from sender nonce (no state mutation; geth: create() addr).
inline void assignCreateAddresses(evmc_address& executionAddress, evmc_message& message,
    bcos::bytesConstRef initCode, state::State& st) noexcept
{
    if (!isCreateKind(message.kind))
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

/// Initialize CREATE target account (nonce=1, warm pin). Must run inside a checkpoint.
inline void initializeCreateTargetAccount(state::State& st, evmc_address const& createAddr,
    evmc_revision revision, bool warmAccess) noexcept
{
    if (state::isZeroAddress(createAddr))
    {
        return;
    }

    if (warmAccess)
    {
        st.pin_warm_create_address(createAddr);
    }

    if (revision >= EVMC_SPURIOUS_DRAGON)
    {
        st.set_nonce(createAddr, 1);
    }
}

/// Assign addresses, warm, and nonce=1 before initcode runs (geth: create account touch).
inline void setupCreateTarget(state::State& st, evmc_address& executionAddress, evmc_message& message,
    evmc_revision revision, bcos::bytesConstRef initCode, bool warmAccess) noexcept
{
    assignCreateAddresses(executionAddress, message, initCode, st);
    initializeCreateTargetAccount(st, message.recipient, revision, warmAccess);
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
