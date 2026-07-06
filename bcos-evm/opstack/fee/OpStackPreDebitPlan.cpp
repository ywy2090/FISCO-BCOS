#include "bcos-evm/opstack/fee/OpStackPreDebitPlan.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include <algorithm>
#include <utility>
#include "bcos-utilities/Common.h"
#include "eth/gas/TxFeeSettlement.h"
#include "opstack/fee/RollupCost.h"

namespace bcos::evm
{

OpStackPreDebitPlan planOpStackPreDebit(
    OpStackPreDebitInputs const& inputs, OpStackFeeHooks const& hooks)
{
    OpStackPreDebitPlan plan;
    plan.core1559 = gas::planPreExecution(inputs.fee);

    plan.sidecar.baseFee = inputs.fee.baseFee;
    plan.sidecar.effectiveGasPrice = plan.core1559.effectiveGasPrice;

    auto totalDebit = plan.core1559.preDebitAmount;

    if (hooks.l1CostFunc != nullptr && inputs.rollupCostData != nullptr)
    {
        plan.sidecar.l1CostCharged =
            (*hooks.l1CostFunc)(inputs.rollupCostData->value(), inputs.blockTime);
        totalDebit += plan.sidecar.l1CostCharged;
    }

    if (hooks.operatorCostFunc != nullptr)
    {
        plan.sidecar.operatorCostLimit =
            (*hooks.operatorCostFunc)(static_cast<uint64_t>(inputs.fee.gasLimit), inputs.blockTime);
        totalDebit += plan.sidecar.operatorCostLimit;
    }

    if (inputs.blobCount > 0)
    {
        auto const blobGasUsed = bcos::u256(inputs.blobCount) * OP_BLOB_GAS_PER_BLOB;
        plan.blobDebit = blobGasUsed * inputs.blobBaseFee;
        plan.blobBalanceCheck = blobGasUsed * inputs.blobGasFeeCap;
        totalDebit += plan.blobDebit;
    }

    plan.totalDebit = totalDebit;
    plan.balanceCheck = totalDebit + inputs.txValue;
    if (inputs.hasGasFeeCap)
    {
        plan.balanceCheck = plan.core1559.maxBalanceDebit + plan.sidecar.l1CostCharged +
                            plan.sidecar.operatorCostLimit + plan.blobBalanceCheck + inputs.txValue;
    }

    return plan;
}

}  // namespace bcos::evm
