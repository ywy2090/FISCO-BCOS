/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Transaction intrinsic gas (pre-EVM debit components).
 * @file TxIntrinsicGas.h
 *
 * Lean model (geth-aligned):
 *   Full intrinsic pre-debit before EVM entry (kernel deductIntrinsicGas).
 *
 * Post-EVM top-level settlement lives in TopLevelGasSettlement.h.
 */

#pragma once

#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include <evmc/evmc.h>
#include <algorithm>

namespace bcos::evm
{
namespace gas
{

/// EIP-2930: sum of per-address and per-storage-key warm charges debited in intrinsic path.
inline int64_t calcAccessListCost(Eip2930AccessList const* accessList) noexcept
{
    if (accessList == nullptr)
    {
        return 0;
    }
    int64_t cost = 0;
    for (auto const& entry : *accessList)
    {
        cost += ACCESS_LIST_ADDRESS_COST;
        cost += static_cast<int64_t>(entry.second.size()) * ACCESS_LIST_STORAGE_KEY_COST;
    }
    return cost;
}

/// EIP-7702: PER_EMPTY_ACCOUNT_COST per authorization tuple (debited in intrinsic path when
/// active).
inline int64_t calcAuthTupleIntrinsicGas(uint64_t authTupleCount) noexcept
{
    return static_cast<int64_t>(authTupleCount) * static_cast<int64_t>(PER_EMPTY_ACCOUNT_COST);
}

/// CREATE/CREATE2 intrinsic: CREATE_BASE_GAS + EIP-3860 initcode word cost (when active).
/// eip3860 comes from RevisionConfig (single source of truth for fork gating) — callers must
/// pass it explicitly; there is no default so an omitted flag is a compile error.
inline int64_t calcCreateIntrinsic(evmc_message const& message, bool eip3860) noexcept
{
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
    {
        return 0;
    }
    int64_t initcodeWordGas = 0;
    if (eip3860)
    {
        auto const inputSize = static_cast<int64_t>(message.input_size);
        auto const words = (inputSize + 31) / 32;
        initcodeWordGas = INITCODE_WORD_GAS * words;
    }
    return CREATE_BASE_GAS + initcodeWordGas;
}

/// Aggregate intrinsic components for a top-level message (geth IntrinsicGas).
/// Used by deductIntrinsicGas, txpool validator, and gasLimitMinimum prechecks.
/// web3TypedTxKind reserved for future typed-tx intrinsic rules; currently unused.
/// eip3860 comes from RevisionConfig.eip3860 — no default, so callers can't silently
/// inherit the wrong fork's gating.
inline TxIntrinsicGas computeTxIntrinsicGas(evmc_message const& message,
    Eip2930AccessList const* accessList, uint8_t web3TypedTxKind, bool eip3860,
    evmc_revision revision)
{
    (void)web3TypedTxKind;
    TxIntrinsicGas intrinsic;
    auto const components = gas::calcEip7623Components(
        bcos::bytesConstRef(message.input_data, message.input_size), revision);
    intrinsic.normalCalldata = components.normalCost;
    intrinsic.floorReserve = components.floorCost;  // data-floor term for gasLimitMinimum

    if (accessList != nullptr && !accessList->empty())
    {
        intrinsic.accessListCost = calcAccessListCost(accessList);
    }

    intrinsic.createIntrinsic = calcCreateIntrinsic(message, eip3860);
    return intrinsic;
}

/// Backward-compatible overload (post-Istanbul calldata rate).
inline TxIntrinsicGas computeTxIntrinsicGas(evmc_message const& message,
    Eip2930AccessList const* accessList, uint8_t web3TypedTxKind, bool eip3860)
{
    return computeTxIntrinsicGas(message, accessList, web3TypedTxKind, eip3860, EVMC_LONDON);
}

}  // namespace gas
}  // namespace bcos::evm

namespace bcos::executor_v1::gas
{
using bcos::evm::gas::computeTxIntrinsicGas;
using bcos::evm::gas::TxIntrinsicGas;
}  // namespace bcos::executor_v1::gas
