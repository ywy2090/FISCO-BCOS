/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Top-level post-EVM gasUsed settlement (EIP-3529 refund cap, EIP-7623 floor).
 * @file TopLevelGasSettlement.h
 *
 * Applies only to the outermost transaction frame after innerExecute returns.
 * Inner frames use evmone refund accounting directly; peak+floor model is tx-level only.
 * Intrinsic gas math lives in TxIntrinsicGas.h.
 *
 * geth anchor: state_transition.go refund finalization + EIP-7623 FloorDataGas uplift.
 */

#pragma once

#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include <algorithm>

namespace bcos::evm::gas
{

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

/// Top-level settlement (geth peakGasUsed model):
///   1. peakGasUsed = gasLimit - min(gasLimit, gasLeft)
///   2. effectiveRefund = min(stateRefund, peakGasUsed / 5)   [EIP-3529]
///   3. gasRemaining = min(gasLimit, gasLeft + effectiveRefund)
///   4. gasUsed = gasLimit - gasRemaining
///   5. uplift to floorDataGas when EIP-7623 floor exceeds computed gasUsed
/// stateRefund must come from host state.get_refund(), not evmc_result.gas_refund alone.
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
    // EIP-7623: final gasUsed cannot fall below calldata floor (capped at gasLimit).
    if (floorDataGas > 0 && gasUsed < floorDataGas)
    {
        gasUsed = std::min(floorDataGas, std::max<int64_t>(0, gasLimit));
    }
    return gasUsed;
}

/// Convenience overload: derives floorDataGas from RevisionConfig::calldata_floor_per_token.
inline int64_t settleTopLevelTransactionGas(int64_t gasLimit, int64_t evmGasLeft,
    int64_t stateRefund, uint8_t calldataFloorPerToken, Eip7623Components const& calldata) noexcept
{
    return settleTopLevelTransactionGas(
        gasLimit, evmGasLeft, stateRefund, calcFloorDataGas(calldataFloorPerToken, calldata));
}

}  // namespace bcos::evm::gas
