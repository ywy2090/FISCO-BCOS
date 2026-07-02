#include "bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/OpStackFeeParams.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSettlement.h"

namespace bcos::evm
{
namespace
{
struct NormalSettleOutcome
{
    OpStackTxFinalizeResult settled;
    OpStackPostSettlementPlan feePlan;
};

/// Populate OP Stack receipt metadata (l1Fee, operatorFee, Jovian daFootprint).
void projectNormalReceiptMeta(OpStackMessageResult& output, OpStackSettlementProjection& view,
    OpStackFeeParams const& feeParams, OpStackTxFinalizeResult const& settled,
    OpStackPostSettlementPlan const& feePlan)
{
    auto const& input = view.input;
    output.receiptMeta.l1Fee = feePlan.l1FeeRouted;
    // Isthmus+: expose operator fee and scalar/constant on receipt when active.
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
    // Jovian: daFootprint = estimatedDASize * daFootprintGasScalar (receipt metadata).
    if (isOpStackJovian(input.forkSchedule, view.blockInfo().timestamp))
    {
        auto const scalar = static_cast<uint64_t>(feeParams.daFootprintGasScalar);
        output.receiptMeta.daFootprintGasScalar = scalar;
        auto const& rollup = view.rollupCostData();
        auto const size = rollup.has_value() ? estimatedDASize(*rollup) : 0;
        output.receiptMeta.daFootprint = size * scalar;
    }
}

/// Post-EVM settlement: gas metering → refundGas → gas pool release.
task::Task<NormalSettleOutcome> settleNormal(OpStackSettlementProjection view,
    StateTransitionExitKind exitKind, OpStackFeeSettlement& ledger, GasPoolHooks const& gasPool)
{
    auto& ctx = view.pipelineContext();
    auto settled = finalizeNormal(ctx, view.feeSidecar(), exitKind);
    auto feePlan = co_await ledger.refundGas(view, settled);
    // Return unused gas limit and report consumed gas to block pool.
    if (gasPool.returnGas)
    {
        gasPool.returnGas(
            settled.gasRemaining, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return NormalSettleOutcome{.settled = settled, .feePlan = feePlan};
}
}  // namespace

task::Task<bool> OpStackNormalTxFeeCoordinator::buyGas(
    OpStackSettlementProjection view, GasPoolHooks const& gasPool, OpStackMessageResult& output)
{
    auto& ctx = view.pipelineContext();
    auto const ok = co_await ledger.buyGas(view);
    if (!ok)
    {
        // Insufficient balance: revert checkpoint, release gas pool, surface evmcResult.
        abortNormalAfterBuyGas(ctx, gasPool, output, ctx.originalGasLimit);
        output.evmcResult = std::move(ctx.evmcResult);
        co_return false;
    }
    co_return true;
}

task::Task<void> OpStackNormalTxFeeCoordinator::completeAfterPipeline(
    OpStackSettlementProjection view, OpStackFeeParams const& feeParams,
    GasPoolHooks const& gasPool, OpStackMessageResult& output)
{
    auto& ctx = view.pipelineContext();
    if (isNormalPreExecutionReject(ctx.exitKind))
    {
        // Intrinsic/gas-afford reject before EVM: no gas charged, revert buyGas debit.
        abortNormalAfterBuyGas(ctx, gasPool, output, ctx.originalGasLimit);
        co_return;
    }

    ctx.state.commit();

    // EVM checkpoint committed; finalize gas metering, refund balances, project receipt.
    auto outcome = co_await settleNormal(view, ctx.exitKind, ledger, gasPool);
    output.gasUsed = outcome.settled.gasUsed;
    projectNormalReceiptMeta(output, view, feeParams, outcome.settled, outcome.feePlan);
    output.stateDiff = ctx.state.build_diff();
}

}  // namespace bcos::evm
