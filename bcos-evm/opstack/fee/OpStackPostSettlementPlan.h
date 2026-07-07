/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack post-settlement fee plan (state-free).
 * @file OpStackPostSettlementPlan.h
 *
 * Runs after EVM execution and gas metering. Splits pre-debited wei among recipients.
 *
 * Core formulas (planOpStackPostSettlement):
 *   core1559        = gas::planPostExecution(fee, gasUsed, gasRemaining)
 *   unusedRefund    = gasRemaining * effectiveGasPrice
 *   coinbaseTip     = gasUsed * (effectiveGasPrice - baseFee)
 *   senderNetDebit  = gasUsed * effectiveGasPrice
 *   l1FeeRouted     = l1CostCharged                    [fixed from buyGas]
 *   operatorFeeCharged = operatorCostFunc(gasUsed, blockTime)
 *   senderOperatorRefund = max(0, operatorCostLimit - operatorFeeCharged)
 */
#pragma once

#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include <bcos-utilities/Common.h>
#include <cstdint>

namespace bcos::evm
{

struct OpStackFeeHooks;

struct OpStackPostSettlementInputs
{
    gas::FeeInputs fee;
    int64_t gasUsed{0};
    int64_t gasRemaining{0};
    uint64_t blockTime{0};
    bcos::u256 l1CostCharged{0};      ///< From buyGas sidecar; routed unchanged to L1 vault
    bcos::u256 operatorCostLimit{0};  ///< Pre-debited upper bound for operator fee refund math
};

struct OpStackPostSettlementPlan
{
    gas::FeeSettlementPlan core1559;
    bcos::u256 l1FeeRouted{0};           ///< Routed to OP_L1_FEE_VAULT_PREDEPLOY
    bcos::u256 operatorFeeCharged{0};    ///< Actual operator fee at gasUsed
    bcos::u256 senderOperatorRefund{0};  ///< Excess pre-debit returned to sender
};

OpStackPostSettlementPlan planOpStackPostSettlement(
    OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks) noexcept;

}  // namespace bcos::evm
