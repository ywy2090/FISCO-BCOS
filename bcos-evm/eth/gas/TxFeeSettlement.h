/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief EIP-1559 fee settlement projection (sync, State-free).
 * @file TxFeeSettlement.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/Eip1559.h"
#include "bcos-utilities/Common.h"
#include <algorithm>

namespace bcos::evm::gas
{

struct FeeInputs
{
    bcos::evm_standard::RevisionConfig const& revision;
    bcos::u256 baseFee;
    int64_t gasLimit{0};
    bcos::u256 gasPrice;
    bcos::u256 gasTipCap;
    bcos::u256 gasFeeCap;
    uint8_t web3TypedTxKind{0};
    bool hasExplicitFeeCaps{false};
};

struct FeeSettlementPlan
{
    bcos::u256 effectiveGasPrice;
    bcos::u256 maxBalanceDebit;
    bcos::u256 preDebitAmount;
    bcos::u256 unusedRefund;
    bcos::u256 coinbaseTip;
    bcos::u256 baseFeeAmount;
    bcos::u256 senderNetDebit;
};

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
