/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Post-execution top-level gasUsed metering (State-free).
 * @file PostExecuteGasMetering.h
 *
 * Eth reference path (applyEthMessage → EthNormalTxFeeCoordinator). Converts pipeline exit
 * kind + EVM outputs into gasUsed/gasRemaining before refundGas maps units to wei.
 * Does not touch State; pre-exec reject is handled by coordinator via abortEthAfterBuyGas.
 */

#pragma once

#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include "bcos-evm/eth/gas/TopLevelGasSettlement.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include <algorithm>

namespace bcos::evm::gas
{

/// IntrinsicRejected / GasAffordRejected: buyGas checkpoint is reverted; no gas charged.
inline bool isPreExecutionGasReject(StateTransitionExitKind exitKind) noexcept
{
    return exitKind == StateTransitionExitKind::IntrinsicRejected ||
           exitKind == StateTransitionExitKind::GasAffordRejected;
}

/// Eth reference entry-rule reject (GST expectException): revert buyGas debit, gasUsed=0.
inline bool isEthRulesEntryRejectAbort(StateTransitionExitKind exitKind) noexcept
{
    return exitKind == StateTransitionExitKind::RulesRejected;
}

/// Post-execution gasUsed/gasRemaining for Eth normal txs.
/// EIP-7623 peak settlement when revision.eip7623 && snapshot.eip7623SnapshotActive;
/// otherwise legacy gasLimit - gasLeft (pre-Prague / no snapshot capture).
inline PostExecuteGasResult meterPostExecuteGas(int64_t originalGasLimit,
    StateTransitionExitKind exitKind, bool eip7623, uint8_t calldataFloorPerToken,
    int64_t evmcGasLeft, TxGasSettlementContext const& snapshot) noexcept
{
    PostExecuteGasResult out{};
    if (isPreExecutionGasReject(exitKind))
    {
        out.gasUsed = 0;
        out.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, originalGasLimit));
        return out;
    }

    if (exitKind == StateTransitionExitKind::Completed ||
        exitKind == StateTransitionExitKind::ExceptionHandled)
    {
        if (eip7623 && snapshot.eip7623SnapshotActive)
        {
            out.gasUsed = settleTopLevelTransactionGas(originalGasLimit, evmcGasLeft,
                snapshot.evmGasRefund, calldataFloorPerToken, snapshot.calldata);
        }
        else
        {
            // Pre-7623: EIP-3529 refund cap still applies (London–Cancun reference path).
            out.gasUsed = settleTopLevelTransactionGas(
                originalGasLimit, evmcGasLeft, snapshot.evmGasRefund, 0);
        }
        out.gasRemaining =
            static_cast<uint64_t>(std::max<int64_t>(0, originalGasLimit - out.gasUsed));
    }
    return out;
}

}  // namespace bcos::evm::gas
