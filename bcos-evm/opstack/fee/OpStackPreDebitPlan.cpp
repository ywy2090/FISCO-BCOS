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
    // core1559: preDebitAmount = gasLimit * effectiveGasPrice
    plan.core1559 = gas::planPreExecution(inputs.fee);

    plan.sidecar.baseFee = inputs.fee.baseFee;
    plan.sidecar.effectiveGasPrice = plan.core1559.effectiveGasPrice;

    auto totalDebit = plan.core1559.preDebitAmount;

    if (hooks.l1CostFunc != nullptr && inputs.rollupCostData != nullptr)
    {
        // L1 fee is charged in full at buyGas; not recomputed from actual gasUsed.
        plan.sidecar.l1CostCharged =
            (*hooks.l1CostFunc)(inputs.rollupCostData->value(), inputs.blockTime);
        totalDebit += plan.sidecar.l1CostCharged;
    }

    if (hooks.operatorCostFunc != nullptr)
    {
        // Reserve operator fee upper bound at gasLimit; excess refunded post-execution.
        plan.sidecar.operatorCostLimit =
            (*hooks.operatorCostFunc)(static_cast<uint64_t>(inputs.fee.gasLimit), inputs.blockTime);
        totalDebit += plan.sidecar.operatorCostLimit;
    }

    if (inputs.blobCount > 0)
    {
        // blobGasUsed = blobCount * OP_BLOB_GAS_PER_BLOB (EIP-4844)
        auto const blobGasUsed = bcos::u256(inputs.blobCount) * OP_BLOB_GAS_PER_BLOB;
        plan.blobDebit = blobGasUsed * inputs.blobBaseFee;
        plan.blobBalanceCheck = blobGasUsed * inputs.blobGasFeeCap;
        totalDebit += plan.blobDebit;
    }

    plan.totalDebit = totalDebit;
    plan.balanceCheck = totalDebit + inputs.txValue;
    if (inputs.hasGasFeeCap)
    {
        // EIP-1559: affordability uses maxBalanceDebit (feeCap-based), not effective price.
        plan.balanceCheck = plan.core1559.maxBalanceDebit + plan.sidecar.l1CostCharged +
                            plan.sidecar.operatorCostLimit + plan.blobBalanceCheck + inputs.txValue;
    }

    return plan;
}

}  // namespace bcos::evm
