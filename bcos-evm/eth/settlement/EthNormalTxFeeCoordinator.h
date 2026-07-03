/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Normal Eth tx fee lifecycle orchestrator.
 * @file EthNormalTxFeeCoordinator.h
 *
 * Wires ApplyEthMessage pipeline stages for normal txs:
 *   buyGas → stateTransitionExecute → completeAfterPipeline
 *        (debit)      (EVM)              (refund + receipt meta)
 *
 * Kernel owns EVM overlay commit/revert; fee layer never reverts on vmerr.
 */

#pragma once

#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/settlement/EthSettlementProjection.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{

struct EthFeeSettlement;

/// Normal Eth tx fee coordinator: buyGas / post-execute metering / refundGas.
struct EthNormalTxFeeCoordinator
{
    EthFeeSettlement& ledger;

    /// Debits sender via ledger. On penalty failure fills @p output; returns false (no revert).
    task::Task<bool> buyGas(EthSettlementProjection view, EthMessageResult& output);

    /// Pre-execution reject → abort; else finalize, refund, gasUsed, stateDiff.
    task::Task<void> completeAfterPipeline(EthSettlementProjection view, EthMessageResult& output);
};

}  // namespace bcos::evm
