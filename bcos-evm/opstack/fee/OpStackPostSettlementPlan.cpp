/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack post-settlement fee plan.
 * @file OpStackPostSettlementPlan.cpp
 */
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "eth/gas/TxFeeSettlement.h"
#include "opstack/fee/OpStackPreDebitPlan.h"
#include <algorithm>
#include <functional>

namespace bcos::evm
{

OpStackPostSettlementPlan planOpStackPostSettlement(
    OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks) noexcept
{
    OpStackPostSettlementPlan plan;
    plan.core1559 = gas::planPostExecution(inputs.fee, inputs.gasUsed, inputs.gasRemaining);
    // L1 fee was fully charged at buyGas; route the same amount to the L1 fee vault.
    plan.l1FeeRouted = inputs.l1CostCharged;

    if (hooks.operatorCostFunc != nullptr)
    {
        auto const used = static_cast<uint64_t>(std::max<int64_t>(0, inputs.gasUsed));
        // operatorFeeCharged uses actual gasUsed (Isthmus/Jovian formula at settlement time).
        plan.operatorFeeCharged = (*hooks.operatorCostFunc)(used, inputs.blockTime);
        // senderOperatorRefund = operatorCostLimit - operatorFeeCharged when pre-charge > actual.
        if (plan.operatorFeeCharged < inputs.operatorCostLimit)
        {
            plan.senderOperatorRefund = inputs.operatorCostLimit - plan.operatorFeeCharged;
        }
    }
    return plan;
}

}  // namespace bcos::evm
