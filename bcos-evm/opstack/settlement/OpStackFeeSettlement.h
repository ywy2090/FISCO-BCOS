/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Ledger-layer fee debit and post-execution refund routing.
 * @file OpStackFeeSettlement.h
 *
 * Implements op-geth StateProcessor buyGas / refundGas for OP Stack:
 *   buyGas    — pre-debit sender: EIP-1559 effective gas + L1 data fee + operator fee + blob fee
 *   refundGas — unused gas refund + route base/L1/operator fees to predeploy recipients
 *
 * Fee math lives in opstack/fee planners (planOpStackPreDebit / planOpStackPostSettlement); this
 * struct owns balance mutations and recipient addresses only.
 */

#pragma once

#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include <bcos-task/Task.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <functional>

namespace bcos::evm
{
#define OP_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_TX_EXECUTOR")

struct OpStackSettlementProjection;
struct OpStackTxFinalizeResult;

struct OpStackFeeSettlement
{
    /// Fork-aware L1 cost estimator (Fjord linear regression over RollupCostData).
    std::function<u256(const RollupCostData&, uint64_t blockTime)> m_l1CostFunc;
    /// Fork-aware operator fee ceiling (Isthmus scalar+constant; Jovian adds daFootprint).
    std::function<u256(uint64_t gasLimit, uint64_t blockTime)> m_operatorCostFunc;
    /// OP Stack predeploy fee recipients (OpStackConstants.h); fees are credited, not burned.
    evmc_address m_baseFeeRecipient = OP_BASE_FEE_RECIPIENT;
    evmc_address m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    evmc_address m_operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;

    /// Pre-execution debit: EIP-1559 + L1 + operator + blob. Skipped for eth_call and deposits.
    task::Task<bool> buyGas(OpStackSettlementProjection view);
    /// Post-execution: unused gas refund to sender; route fees to coinbase + predeploy recipients.
    task::Task<OpStackPostSettlementPlan> refundGas(
        OpStackSettlementProjection& view, OpStackTxFinalizeResult const& settled);
};

}  // namespace bcos::evm
