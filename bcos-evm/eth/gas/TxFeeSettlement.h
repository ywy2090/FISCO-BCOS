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

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip1559.h"
#include "bcos-utilities/Common.h"
#include <algorithm>

namespace bcos::evm::gas
{

/// Inputs required to price a single transaction under the active fee market.
struct FeeInputs
{
    bcos::evm::RevisionConfig const& revision;
    bcos::u256 baseFee;  ///< Block base fee (wei); from blockInfo, not recomputed here
    int64_t gasLimit{0};
    bcos::u256 gasPrice;             ///< Legacy gasPrice field (pre-1559 or fallback)
    bcos::u256 gasTipCap;            ///< maxPriorityFeePerGas
    bcos::u256 gasFeeCap;            ///< maxFeePerGas
    uint8_t web3TypedTxKind{0};      ///< EIP-2718 type byte (0x02 / 0x04 → 1559 caps)
    bool hasExplicitFeeCaps{false};  ///< Typed tx or explicit cap fields from TE builder
};

/// Projected wei amounts for buyGas (pre) and refundGas (post). Orchestrator maps fields to State.
struct FeeSettlementPlan
{
    // --- planPreExecution (执行前填写的三个字段) ---

    /// 实际 gas 单价 (wei/gas)。
    /// 1559: min(gasFeeCap, gasTipCap + baseFee)；未启用 1559 / Legacy: 退化为 gasPrice。
    /// 用于 buyGas 预扣、refundGas 退还未用 gas、GASPRICE opcode、receipt gasPrice 字段。
    bcos::u256 effectiveGasPrice;

    /// 余额充足性检查上限 (wei)，不是实际扣款。
    /// 1559: gasLimit × gasFeeCap；Legacy: gasLimit × gasPrice。
    /// buyGas 要求 balance >= maxBalanceDebit + txValue（最坏情况仍能付满 gasLimit）。
    bcos::u256 maxBalanceDebit;

    /// buyGas 从 sender 实际预扣的 wei：gasLimit × effectiveGasPrice。
    /// 执行后 refundGas 退 (gasLimit - gasUsed) × effectiveGasPrice；sender 净支出 gasUsed ×
    /// effective。
    bcos::u256 preDebitAmount;

    // --- planPostExecution (执行后填写；pre 阶段为零) ---

    /// 退还给 sender 的 wei：gasRemaining × effectiveGasPrice。
    bcos::u256 unusedRefund;

    /// 付给 coinbase 的小费 wei：gasUsed × max(0, effectiveGasPrice - baseFee)。
    bcos::u256 coinbaseTip;

    /// base fee 分量 wei：gasUsed × baseFee。Eth 路径隐式销毁；OpStack 路由至
    /// OP_BASE_FEE_RECIPIENT。
    bcos::u256 baseFeeAmount;

    /// sender 执行 gas 净支出 wei：gasUsed × effectiveGasPrice（= baseFeeAmount + coinbaseTip
    /// 分量之和）。
    bcos::u256 senderNetDebit;
};

/// Pre-execution projection: effective price, affordability cap, and buyGas pre-debit amount.
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

/// Post-execution projection: splits consumed gas into sender refund, coinbase tip, and base fee.
/// gasRemaining is unused gas units after final gasUsed (typically gasLimit - gasUsed).
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
