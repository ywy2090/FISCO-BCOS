/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Maps OpStackSettlementProjection + OpStackTxFinalizeResult into
 * OpStackPostSettlementInputs.
 * @file OpStackPostSettlementInputsMapping.h
 */

#pragma once

#include "bcos-evm/opstack/fee/OpStackFeeInputsMapping.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementProjection.h"
#include "bcos-evm/opstack/settlement/OpStackTxFinalize.h"

namespace bcos::evm
{

inline OpStackPostSettlementInputs toOpStackPostSettlementInputs(
    OpStackSettlementProjection const& view, OpStackTxFinalizeResult const& settled) noexcept
{
    auto const& sidecar = view.feeSidecar();
    return OpStackPostSettlementInputs{
        .fee = gas::toFeeInputs(view, view.pipelineContext().originalGasLimit),
        .gasUsed = settled.gasUsed,
        .gasRemaining = static_cast<int64_t>(settled.gasRemaining),
        .blockTime = static_cast<uint64_t>(view.blockInfo().timestamp),
        .l1CostCharged = sidecar.l1CostCharged,
        .operatorCostLimit = sidecar.operatorCostLimit,
    };
}

}  // namespace bcos::evm
