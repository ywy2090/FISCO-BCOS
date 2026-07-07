/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Pure projection of Op Stack fee fields onto receipt metadata.
 * @file OpStackReceiptMetaProjection.h
 *
 * Receipt-only fields; no balance mutations. Called after refundGas completes.
 *
 * Core formulas:
 *   l1Fee              = feePlan.l1FeeRouted
 *   operatorFee        = feePlan.operatorFeeCharged              [Isthmus+, when wired]
 *   daFootprint        = estimatedDASize(rollupCostData) * daFootprintGasScalar  [Jovian]
 *   daFootprintGasScalar = feeParams.daFootprintGasScalar       [Jovian, from L1Block slot 8]
 *
 * Mirrors op-geth receipt extensions (L1Fee, OperatorFee, BlobGasUsed Jovian semantics).
 */

#pragma once

#include "bcos-evm/opstack/fee/OpStackFeeParams.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-evm/opstack/types/OpStackReceiptMeta.h"
#include <optional>

namespace bcos::evm
{

struct OpStackReceiptMetaProjectionInput
{
    OpStackForkSchedule const& forkSchedule;
    uint64_t blockTime{0};
    bool hasOperatorCostFunc{false};
    OpStackFeeParams const& feeParams;
    OpStackPostSettlementPlan const& feePlan;
    std::optional<RollupCostData> const& rollupCostData;
};

/// Populate @p out from settlement outcome. Existing optional fields are overwritten when active.
inline void projectOpStackReceiptMeta(
    OpStackReceiptMeta& out, OpStackReceiptMetaProjectionInput const& input) noexcept
{
    out.l1Fee = input.feePlan.l1FeeRouted;

    if (isOpStackIsthmus(input.forkSchedule, input.blockTime) && input.hasOperatorCostFunc)
    {
        out.operatorFee = input.feePlan.operatorFeeCharged;
        if (input.feeParams.operatorFeeScalar != 0 || input.feeParams.operatorFeeConstant != 0)
        {
            out.operatorFeeScalar = input.feeParams.operatorFeeScalar;
            out.operatorFeeConstant = input.feeParams.operatorFeeConstant;
        }
    }

    if (isOpStackJovian(input.forkSchedule, input.blockTime))
    {
        auto const scalar = static_cast<uint64_t>(input.feeParams.daFootprintGasScalar);
        out.daFootprintGasScalar = scalar;
        // daFootprint = estimatedDASize * daFootprintGasScalar
        auto const size =
            input.rollupCostData.has_value() ? estimatedDASize(*input.rollupCostData) : 0;
        out.daFootprint = size * scalar;
    }
}

}  // namespace bcos::evm
