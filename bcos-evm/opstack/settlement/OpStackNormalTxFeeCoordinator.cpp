#include "bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/OpStackFeeParams.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSettlement.h"

namespace bcos::evm
{
namespace
{
struct NormalSettleOutcome
{
    OpStackSettlementResult settled;
    OpStackPostSettlementPlan feePlan;
};

void projectNormalReceiptMeta(OpStackMessageResult& output, OpStackSettlementFacade& view,
    OpStackFeeParams const& feeParams, OpStackSettlementResult const& settled,
    OpStackPostSettlementPlan const& feePlan)
{
    auto const& input = view.input;
    output.receiptMeta.l1Fee = feePlan.l1FeeRouted;
    if (isOpStackIsthmus(input.forkSchedule, view.blockInfo().timestamp) &&
        input.opTxExecutor.m_operatorCostFunc)
    {
        output.receiptMeta.operatorFee = feePlan.operatorFeeCharged;
        if (feeParams.operatorFeeScalar != 0 || feeParams.operatorFeeConstant != 0)
        {
            output.receiptMeta.operatorFeeScalar = feeParams.operatorFeeScalar;
            output.receiptMeta.operatorFeeConstant = feeParams.operatorFeeConstant;
        }
    }
}

task::Task<NormalSettleOutcome> settleNormal(OpStackSettlementFacade view,
    StateTransitionExitKind exitKind, OpStackFeeSettlement& ledger, GasPoolHooks const& gasPool)
{
    auto& ctx = view.pipelineContext();
    auto settled = finalizeNormal(ctx, view.feeSidecar(), exitKind);
    auto feePlan = co_await ledger.refundGas(view, settled);
    if (gasPool.returnGas)
    {
        gasPool.returnGas(
            settled.gasRemaining, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return NormalSettleOutcome{.settled = settled, .feePlan = feePlan};
}
}  // namespace

task::Task<bool> OpStackNormalTxFeeCoordinator::buyGas(
    OpStackSettlementFacade view, GasPoolHooks const& gasPool, OpStackMessageResult& output)
{
    auto& ctx = view.pipelineContext();
    auto const ok = co_await ledger.buyGas(view);
    if (!ok)
    {
        abortNormalAfterBuyGas(ctx, gasPool, output, ctx.originalGasLimit);
        output.evmcResult = std::move(ctx.evmcResult);
        co_return false;
    }
    co_return true;
}

task::Task<void> OpStackNormalTxFeeCoordinator::completeAfterPipeline(OpStackSettlementFacade view,
    OpStackFeeParams const& feeParams, GasPoolHooks const& gasPool, OpStackMessageResult& output)
{
    auto& ctx = view.pipelineContext();
    if (isNormalPreExecutionReject(ctx.exitKind))
    {
        abortNormalAfterBuyGas(ctx, gasPool, output, ctx.originalGasLimit);
        co_return;
    }

    ctx.state.commit();

    auto outcome = co_await settleNormal(view, ctx.exitKind, ledger, gasPool);
    output.gasUsed = outcome.settled.gasUsed;
    projectNormalReceiptMeta(output, view, feeParams, outcome.settled, outcome.feePlan);
    output.stateDiff = ctx.state.build_diff();
}

}  // namespace bcos::evm
