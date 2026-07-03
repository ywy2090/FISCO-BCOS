/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief EIP-1559 fee settlement projection (sync, State-free).
 * @file TxFeeSettlement.h
 *
 * Pure wei arithmetic for 1559/legacy gas payment. Does not touch State; orchestration layers
 * (EthTxFeeSettlement, OpStackFeeSettlement) apply the plan to sender/coinbase/recipients.
 *
 * Formula helpers live in eth/eip/Eip1559.h; block baseFee is supplied via FeeInputs (see
 * FeeInputsMapping.h). Block-level baseFee *update* is out of scope here — only consumption.
 */

#pragma once

#include "bcos-evm/eth/eip/Eip1559.h"
#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include <algorithm>

namespace bcos::evm::gas
{

/// Pre-execution wei projection (geth buyGas arithmetic). Populates effectiveGasPrice,
/// maxBalanceDebit (affordability cap), and preDebitAmount (actual pre-debit).
/// Post fields remain zero until planPostExecution.
inline FeeSettlementPlan planPreExecution(FeeInputs const& inputs) noexcept
{
    FeeSettlementPlan plan{};
    auto const caps = normalizeGasCaps(inputs.gasPrice, inputs.gasTipCap, inputs.gasFeeCap,
        inputs.web3TypedTxKind, inputs.hasExplicitFeeCaps, inputs.revision);
    plan.effectiveGasPrice =
        resolveEffectiveGasPrice(caps.gasTipCap, caps.gasFeeCap, inputs.baseFee);
    plan.maxBalanceDebit = maxBalanceGasDebit(inputs.gasLimit, caps);
    if (inputs.gasLimit > 0 && plan.effectiveGasPrice > 0)
    {
        plan.preDebitAmount = bcos::u256(inputs.gasLimit) * plan.effectiveGasPrice;
    }
    return plan;
}

/// Post-execution wei projection (geth refundGas arithmetic).
/// Recomputes effectiveGasPrice from caps, then splits consumed gas into:
///   unusedRefund  → returned to sender
///   coinbaseTip   → priority fee to coinbase (Eth burns baseFeeAmount)
///   senderNetDebit → gasUsed × effectiveGasPrice (net sender cost)
/// gasRemaining is unused gas units after meterPostExecuteGas (typically gasLimit - gasUsed).
inline FeeSettlementPlan planPostExecution(
    FeeInputs const& inputs, int64_t gasUsed, int64_t gasRemaining) noexcept
{
    auto plan = planPreExecution(inputs);
    auto const used = std::max<int64_t>(0, gasUsed);
    auto const remaining = std::max<int64_t>(0, gasRemaining);
    if (plan.effectiveGasPrice == 0)
    {
        return plan;
    }
    if (remaining > 0)
    {
        plan.unusedRefund = bcos::u256(remaining) * plan.effectiveGasPrice;
    }
    if (used > 0)
    {
        plan.coinbaseTip = bcos::u256(used) * tipPerGas(plan.effectiveGasPrice, inputs.baseFee);
        plan.baseFeeAmount = bcos::u256(used) * inputs.baseFee;
        plan.senderNetDebit = bcos::u256(used) * plan.effectiveGasPrice;
    }
    return plan;
}

}  // namespace bcos::evm::gas
