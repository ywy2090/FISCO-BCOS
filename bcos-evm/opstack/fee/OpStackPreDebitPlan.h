/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack pre-execution (buyGas) fee plan — state-free wei arithmetic.
 * @file OpStackPreDebitPlan.h
 *
 * Composes EIP-1559 pre-debit (gas::planPreExecution) with Op Stack sidecar charges.
 * Settlement layer applies totalDebit / balanceCheck to sender balance.
 *
 * Core formulas (planOpStackPreDebit):
 *   effectiveGasPrice = EIP-1559 resolve (see TxFeeSettlement.h)
 *   preDebitAmount    = gasLimit * effectiveGasPrice
 *   l1CostCharged     = l1CostFunc(rollupCostData, blockTime)   [Fjord+, at gasLimit DA estimate]
 *   operatorCostLimit = operatorCostFunc(gasLimit, blockTime)   [Isthmus+, upper bound pre-charge]
 *   blobGasUsed       = blobCount * OP_BLOB_GAS_PER_BLOB
 *   blobDebit         = blobGasUsed * blobBaseFee
 *   blobBalanceCheck  = blobGasUsed * blobGasFeeCap
 *   totalDebit        = preDebitAmount + l1CostCharged + operatorCostLimit + blobDebit
 *   balanceCheck (legacy)     = totalDebit + txValue
 *   balanceCheck (EIP-1559)   = maxBalanceDebit + l1CostCharged + operatorCostLimit
 *                               + blobBalanceCheck + txValue
 */

#pragma once

#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include <bcos-utilities/Common.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace bcos::evm
{

struct RollupCostData;

/// Fork-specific L1 and operator cost calculators wired by OpStackFeeParams.
struct OpStackFeeHooks
{
    std::function<bcos::u256(RollupCostData const&, uint64_t)> const* l1CostFunc{nullptr};
    std::function<bcos::u256(uint64_t gasLimit, uint64_t blockTime)> const* operatorCostFunc{
        nullptr};
};

/// Op Stack fee fields recorded at buyGas for post-settlement and receipt metadata.
struct OpStackFeeSidecarWrite
{
    bcos::u256 effectiveGasPrice{0};
    bcos::u256 baseFee{0};
    bcos::u256 l1CostCharged{0};      ///< Full L1 fee debited up front (not gas-metered)
    bcos::u256 operatorCostLimit{0};  ///< Max operator fee reserved at gasLimit
};

struct OpStackPreDebitInputs
{
    gas::FeeInputs fee;
    bcos::u256 txValue{0};
    uint64_t blockTime{0};
    bool hasGasFeeCap{true};
    bcos::u256 blobGasFeeCap{0};
    bcos::u256 blobBaseFee{0};
    size_t blobCount{0};
    std::optional<RollupCostData> const* rollupCostData{nullptr};
};

struct OpStackPreDebitPlan
{
    gas::FeeSettlementPlan core1559;  ///< EIP-1559 preDebitAmount, maxBalanceDebit, etc.
    OpStackFeeSidecarWrite sidecar;
    bcos::u256 blobDebit{0};          ///< blobGasUsed * blobBaseFee (actual charge)
    bcos::u256 blobBalanceCheck{0};   ///< blobGasUsed * blobGasFeeCap (affordability cap)
    bcos::u256 totalDebit{0};         ///< Wei actually debited from sender at buyGas
    bcos::u256 balanceCheck{0};       ///< Minimum balance required before execution
};

OpStackPreDebitPlan planOpStackPreDebit(
    OpStackPreDebitInputs const& inputs, OpStackFeeHooks const& hooks);

}  // namespace bcos::evm
