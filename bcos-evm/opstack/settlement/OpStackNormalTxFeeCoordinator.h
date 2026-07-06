/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Normal L2 tx fee lifecycle orchestrator.
 * @file OpStackNormalTxFeeCoordinator.h
 *
 * Wires ApplyOpStackMessage pipeline stages for non-deposit txs:
 *   buyGas → stateTransitionExecute → completeAfterPipeline
 *        (debit)      (EVM)              (commit + refund + receipt meta)
 *
 * Deposit txs bypass this coordinator and use settleDeposit directly.
 */

#pragma once

#include <bcos-task/Task.h>

namespace bcos::evm
{

struct GasPoolHooks;
struct OpStackFeeParams;
struct OpStackFeeSettlement;
struct OpStackMessageResult;
struct OpStackSettlementProjection;

/// Normal L2 tx fee coordinator: wraps OpStackFeeSettlement + OpStackTxFinalize.
struct OpStackNormalTxFeeCoordinator
{
    OpStackFeeSettlement& ledger;

    /// Debits sender via ledger. On failure runs abort contract and fills @p output; returns false.
    task::Task<bool> buyGas(OpStackSettlementProjection view, GasPoolHooks const& gasPool,
        OpStackMessageResult& output);

    /// Pre-execution reject → abort; else commit, refund, gas pool, receipt meta, stateDiff.
    task::Task<void> completeAfterPipeline(OpStackSettlementProjection view,
        OpStackFeeParams const& feeParams, GasPoolHooks const& gasPool,
        OpStackMessageResult& output);
};

}  // namespace bcos::evm
