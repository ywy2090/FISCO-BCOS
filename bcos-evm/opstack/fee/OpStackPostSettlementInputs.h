#pragma once

#include "bcos-evm/eth/state-transition/FeeInputsMapping.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"

namespace bcos::evm
{

inline OpStackPostSettlementInputs toOpStackPostSettlementInputs(
    OpStackSettlementFacade const& view, OpStackSettlementResult const& settled) noexcept
{
    auto const& ctx = view.pipelineContext();
    auto const& sidecar = view.feeSidecar();
    return OpStackPostSettlementInputs{
        .fee = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
            gas::FeeCapsView{ctx.gasPrice, view.gasTipCap(), view.gasFeeCap(),
                view.web3TypedTxKind(), view.hasGasFeeCap()},
            ctx.originalGasLimit),
        .gasUsed = settled.gasUsed,
        .gasRemaining = static_cast<int64_t>(settled.gasRemaining),
        .blockTime = static_cast<uint64_t>(view.blockInfo().timestamp),
        .l1CostCharged = sidecar.l1CostCharged,
        .operatorCostLimit = sidecar.operatorCostLimit,
    };
}

}  // namespace bcos::evm
