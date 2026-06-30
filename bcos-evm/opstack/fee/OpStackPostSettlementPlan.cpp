/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack post-settlement fee plan.
 * @file OpStackPostSettlementPlan.cpp
 */
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include <algorithm>

namespace bcos::evm
{

OpStackPostSettlementPlan planOpStackPostSettlement(
    OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks) noexcept
{
    OpStackPostSettlementPlan plan;
    plan.core1559 = gas::planPostExecution(inputs.fee, inputs.gasUsed, inputs.gasRemaining);
    plan.l1FeeRouted = inputs.l1CostCharged;

    if (hooks.operatorCostFunc != nullptr)
    {
        auto const used = static_cast<uint64_t>(std::max<int64_t>(0, inputs.gasUsed));
        plan.operatorFeeCharged = (*hooks.operatorCostFunc)(used, inputs.blockTime);
        if (plan.operatorFeeCharged < inputs.operatorCostLimit)
        {
            plan.senderOperatorRefund = inputs.operatorCostLimit - plan.operatorFeeCharged;
        }
    }
    return plan;
}

}  // namespace bcos::evm
