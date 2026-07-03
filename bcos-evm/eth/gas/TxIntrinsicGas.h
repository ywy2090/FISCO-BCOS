/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Transaction intrinsic gas and top-level gasUsed settlement.
 * @file TxIntrinsicGas.h
 *
 * Lean model (geth-aligned):
 *   1. Full intrinsic pre-debit before EVM entry (kernel deductIntrinsicGas).
 *   2. Post-EVM: gasUsed = gasLimit - min(gasLimit, gasLeft + cappedRefund), with EIP-7623 floor.
 *
 * EIP-3529 refund cap and EIP-7623 calldata floor apply at top-level settlement only;
 * inner frames use standard evmone refund accounting.
 */

#pragma once

#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include <evmc/evmc.h>
#include <algorithm>

namespace bcos::evm
{
namespace gas
{

/// Breakdown of gas debited before EVM entry (intrinsic + access list + CREATE overhead).
struct TxIntrinsicGas
{
    int64_t txBase = TX_BASE_GAS;
    int64_t normalCalldata = 0;   ///< Legacy calldata cost (EIP-7623 "normal" component)
    int64_t accessListCost = 0;   ///< EIP-2930 warm-address/key charges
    int64_t createIntrinsic = 0;  ///< CREATE/CREATE2 base + initcode words
    int64_t floorReserve = 0;     ///< EIP-7623 floor minus txBase (used in gasLimitMinimum)

    int64_t fixedCost() const { return txBase + accessListCost; }

    /// Total intrinsic debited from message.gas before innerExecute.
    int64_t preExecutionDebit() const { return fixedCost() + normalCalldata + createIntrinsic; }

    /// EIP-7623: max(21000 + floor, intrinsic) where intrinsic includes access list + CREATE.
    int64_t gasLimitMinimum() const
    {
        int64_t const intrinsic = preExecutionDebit();
        int64_t const floorTotal = txBase + floorReserve;
        return std::max(intrinsic, floorTotal);
    }

    /// EIP-7623 + EIP-7702: auth intrinsic is part of the intrinsic side of the minimum, not
    /// additive on top of the floor comparison.
    int64_t gasLimitMinimumWithAuth(int64_t authCost) const
    {
        int64_t const intrinsic = preExecutionDebit() + authCost;
        int64_t const floorTotal = txBase + floorReserve;
        return std::max(intrinsic, floorTotal);
    }
};

/// Snapshot captured after EVM for EIP-7623 / EIP-3529 top-level settlement.
struct TxGasSettlementSnapshot
{
    int64_t gasLimit = 0;
    Eip7623Components calldata{};
    int64_t evmGasRefund = 0;  ///< Host refund counter at end of execution (SSTORE refunds, etc.)
};

using TxGasSettlementContext = TxGasSettlementSnapshot;

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

inline int64_t calcCreateIntrinsic(evmc_message const& message) noexcept
{
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
    {
        return 0;
    }
    auto const inputSize = static_cast<int64_t>(message.input_size);
    auto const words = (inputSize + 31) / 32;
    return CREATE_BASE_GAS + INITCODE_WORD_GAS * words;
}

/// Aggregate intrinsic components for a top-level message (used by kernel and txpool prechecks).
inline TxIntrinsicGas computeTxIntrinsicGas(
    evmc_message const& message, Eip2930AccessList const* accessList, uint8_t web3TypedTxKind)
{
    (void)web3TypedTxKind;
    TxIntrinsicGas intrinsic;
    auto const components =
        gas::calcEip7623Components(bcos::bytesConstRef(message.input_data, message.input_size));
    intrinsic.normalCalldata = components.normalCost;
    intrinsic.floorReserve = components.floorCost;

    if (accessList != nullptr && !accessList->empty())
    {
        intrinsic.accessListCost = calcAccessListCost(accessList);
    }

    intrinsic.createIntrinsic = calcCreateIntrinsic(message);
    return intrinsic;
}

/// EIP-3529: refund capped at gasUsed / 5 (replaces pre-London gasUsed / 2 rule).
inline int64_t effectiveRefundEip3529(int64_t evmGasRefund, int64_t gasUsedBeforeRefund) noexcept
{
    if (gasUsedBeforeRefund <= 0)
    {
        return 0;
    }
    return std::min(evmGasRefund, gasUsedBeforeRefund / REFUND_QUOTIENT_EIP3529);
}

/// EIP-7623 minimum data gas: TX_BASE_GAS + tokenCount * calldataFloorPerToken.
inline int64_t calcFloorDataGas(
    uint8_t calldataFloorPerToken, Eip7623Components const& calldata) noexcept
{
    return TX_BASE_GAS + calldata.tokenCount * calldataFloorPerToken;
}

/// Top-level settlement: peakGasUsed from gasLimit/gasLeft, EIP-3529 refund cap,
/// EIP-7623 floor uplift. Authoritative refund counter is host state.get_refund().
inline int64_t settleTopLevelTransactionGas(
    int64_t gasLimit, int64_t evmGasLeft, int64_t stateRefund, int64_t floorDataGas) noexcept
{
    int64_t const gasLeft =
        std::min(std::max<int64_t>(0, evmGasLeft), std::max<int64_t>(0, gasLimit));
    int64_t const peakGasUsed = std::max<int64_t>(0, gasLimit) - gasLeft;
    int64_t const effectiveRefund = effectiveRefundEip3529(stateRefund, peakGasUsed);
    int64_t const gasRemaining =
        std::min(std::max<int64_t>(0, gasLimit), gasLeft + effectiveRefund);
    int64_t gasUsed = std::max<int64_t>(0, gasLimit) - gasRemaining;
    if (floorDataGas > 0 && gasUsed < floorDataGas)
    {
        gasUsed = std::min(floorDataGas, std::max<int64_t>(0, gasLimit));
    }
    return gasUsed;
}

inline int64_t settleTopLevelTransactionGas(int64_t gasLimit, int64_t evmGasLeft,
    int64_t stateRefund, uint8_t calldataFloorPerToken, Eip7623Components const& calldata) noexcept
{
    return settleTopLevelTransactionGas(
        gasLimit, evmGasLeft, stateRefund, calcFloorDataGas(calldataFloorPerToken, calldata));
}

/// Included top-level vmerr (ADR-015): same peak settlement as success path on committed failures.
inline int64_t settleIncludedTopLevelTransactionGas(
    int64_t gasLimit, int64_t evmGasLeft, int64_t stateRefund, int64_t floorDataGas) noexcept
{
    return settleTopLevelTransactionGas(gasLimit, evmGasLeft, stateRefund, floorDataGas);
}

inline int64_t settleIncludedTopLevelTransactionGas(int64_t gasLimit, int64_t evmGasLeft,
    int64_t stateRefund, uint8_t calldataFloorPerToken, Eip7623Components const& calldata) noexcept
{
    return settleTopLevelTransactionGas(
        gasLimit, evmGasLeft, stateRefund, calldataFloorPerToken, calldata);
}

/// Shared TE / EEST top-level gasUsed finalization (GAP-TE-002 Task 3).
/// Chooses legacy (gasLimit - gasLeft) vs EIP-7623 settlement based on fork flags and vmerr path.
inline int64_t finalizeEthTxGasUsed(int64_t gasLimit, int64_t legacyGasLeft, int64_t rawGasUsed,
    bool isWeb3, bool eip7623, bool topLevelIncludedTxVmError,
    TxGasSettlementSnapshot const& snapshot, uint8_t calldataFloorPerToken) noexcept
{
    if (topLevelIncludedTxVmError && eip7623 && snapshot.gasLimit > 0)
    {
        return settleIncludedTopLevelTransactionGas(gasLimit, legacyGasLeft, snapshot.evmGasRefund,
            calldataFloorPerToken, snapshot.calldata);
    }
    if (snapshot.gasLimit > 0 && isWeb3 && eip7623)
    {
        return settleTopLevelTransactionGas(gasLimit, legacyGasLeft, snapshot.evmGasRefund,
            calldataFloorPerToken, snapshot.calldata);
    }
    return rawGasUsed;
}

}  // namespace gas
}  // namespace bcos::evm

namespace bcos::executor_v1::gas
{
using bcos::evm::gas::computeTxIntrinsicGas;
using bcos::evm::gas::TxIntrinsicGas;
}  // namespace bcos::executor_v1::gas
