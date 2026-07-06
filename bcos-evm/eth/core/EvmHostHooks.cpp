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
 * @brief Default `EvmHostHooks` methods and EIP-3529 SSTORE helper implementations.
 * @file EvmHostHooks.cpp
 */

#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/eip/Eip2929StorageGas.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "eth/state/StateKeyHash.hpp"

namespace bcos::evm::state
{
namespace
{
using bcos::evm::gas::COLD_SLOAD_COST_EIP2929;
using bcos::evm::gas::SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529;
using bcos::evm::gas::SSTORE_RESET_GAS_EIP2200;
using bcos::evm::gas::SSTORE_SET_GAS_EIP2200;
using bcos::evm::gas::WARM_STORAGE_READ_COST_EIP2929;
}  // namespace

// EIP-3529 SSTORE refund counter updates before the slot write (geth: gasState.AddRefund).
// Uses tx-original / pre-write current / new value:
//   - clean slot (original == current): clear-to-zero earns SSTORE_CLEARS refund
//   - dirty slot (original != current): adjust prior clear refund; restore-to-original
//     earns SET/RESET gas delta minus warm/cold read costs
void applySstoreRefundEip3529(State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& value) noexcept
{
    if (Bytes32Equal{}(current, value))
    {
        return;
    }
    if (Bytes32Equal{}(original, current))
    {
        if (isZeroBytes32(original))
        {
            return;
        }
        if (isZeroBytes32(value))
        {
            state.add_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
        }
        return;
    }
    if (!isZeroBytes32(original))
    {
        if (isZeroBytes32(current))
        {
            state.sub_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
        }
        else if (isZeroBytes32(value))
        {
            state.add_refund(SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
        }
    }
    if (Bytes32Equal{}(original, value))
    {
        if (isZeroBytes32(original))
        {
            state.add_refund(SSTORE_SET_GAS_EIP2200 - WARM_STORAGE_READ_COST_EIP2929);
        }
        else
        {
            state.add_refund((SSTORE_RESET_GAS_EIP2200 - COLD_SLOAD_COST_EIP2929) -
                             WARM_STORAGE_READ_COST_EIP2929);
        }
    }
}

evmc_storage_status classifyStorageStatusPrecise(evmc_bytes32 const& oldValue,
    evmc_bytes32 const& currentValue, evmc_bytes32 const& newValue) noexcept
{
    // evmone test/state/host.cpp set_storage (EIP-2200 / EIP-3529 status mapping).
    auto const dirty = !Bytes32Equal{}(oldValue, currentValue);
    auto const restored = Bytes32Equal{}(oldValue, newValue);
    auto const currentIsZero = isZeroBytes32(currentValue);
    auto const valueIsZero = isZeroBytes32(newValue);

    auto status = EVMC_STORAGE_ASSIGNED;
    if (!dirty && !restored)
    {
        if (currentIsZero)
        {
            status = EVMC_STORAGE_ADDED;
        }
        else if (valueIsZero)
        {
            status = EVMC_STORAGE_DELETED;
        }
        else
        {
            status = EVMC_STORAGE_MODIFIED;
        }
    }
    else if (dirty && !restored)
    {
        if (currentIsZero && !valueIsZero)
        {
            status = EVMC_STORAGE_DELETED_ADDED;
        }
        else if (!currentIsZero && valueIsZero)
        {
            status = EVMC_STORAGE_MODIFIED_DELETED;
        }
    }
    else if (dirty && restored)
    {
        if (currentIsZero)
        {
            status = EVMC_STORAGE_DELETED_RESTORED;
        }
        else if (valueIsZero)
        {
            status = EVMC_STORAGE_ADDED_DELETED;
        }
        else
        {
            status = EVMC_STORAGE_MODIFIED_RESTORED;
        }
    }
    return status;
}

void EvmHostHooks::applySstoreRefund(State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept
{
    applySstoreRefundEip3529(state, current, original, newValue);
}

evmc_storage_status EvmHostHooks::classifyStorageStatus(evmc_bytes32 const& original,
    evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept
{
    return classifyStorageStatusPrecise(original, current, newValue);
}

// Default no-op: chain override point for legacy deleted-slot refund after status
// classification. FISCO applies when `RevisionFlags::fix_storage_status` is disabled.
void EvmHostHooks::applyLegacySstoreDeletedRefund(
    State& /*state*/, evmc_storage_status /*status*/) const noexcept
{}

// Default no-op: chain override point for top-level CREATE nonce finalization.
// FISCO applies contract-create nonce semantics when `fix_nonce_init` is enabled.
void EvmHostHooks::finalizeTopLevelCreateNonce(
    State& /*state*/, evmc_address const& /*createAddr*/) noexcept
{}
}  // namespace bcos::evm::state
