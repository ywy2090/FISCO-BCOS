/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Maps OpStackSettlementFacade into OpStackPreDebitInputs (types in OpStackPreDebitPlan.h).
 * @file OpStackPreDebitInputsMapping.h
 */

#pragma once

#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/fee/OpStackPreDebitPlan.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementFacade.h"

namespace bcos::evm
{

inline OpStackPreDebitInputs toOpStackPreDebitInputs(OpStackSettlementFacade const& view) noexcept
{
    auto const& ctx = view.pipelineContext();
    std::optional<RollupCostData> const* rollupPtr = nullptr;
    auto const& rollup = view.rollupCostData();
    if (rollup.has_value())
    {
        rollupPtr = std::addressof(rollup);
    }
    return OpStackPreDebitInputs{
        .fee = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
            gas::FeeCapsView{ctx.gasPrice, view.gasTipCap(), view.gasFeeCap(),
                view.web3TypedTxKind(), view.hasGasFeeCap()},
            ctx.originalGasLimit),
        .txValue = state::fromEvmC(ctx.message.value),
        .blockTime = static_cast<uint64_t>(view.blockInfo().timestamp),
        .hasGasFeeCap = view.hasGasFeeCap(),
        .blobGasFeeCap = view.blobGasFeeCap(),
        .blobBaseFee = view.blockInfo().blobBaseFee,
        .blobCount = view.blobVersionedHashes().size(),
        .rollupCostData = rollupPtr,
    };
}

}  // namespace bcos::evm
