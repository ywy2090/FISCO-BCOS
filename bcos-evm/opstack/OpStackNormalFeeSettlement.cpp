#include "bcos-evm/opstack/OpStackNormalFeeSettlement.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "bcos-evm/opstack/fee/OpStackFee.h"

namespace bcos::evm
{
namespace
{
void projectNormalReceiptMeta(OpStackExecutionResult& output, OpStackSettlementView& view,
    OpStackFeeParams const& feeParams, OpStackSettlementResult const& settled)
{
    auto const& input = view.input;
    auto const& sidecar = view.feeSidecar();

    output.receiptMeta.l1Fee = sidecar.l1CostCharged;
    if (isOpStackIsthmus(input.forkSchedule, view.blockInfo().timestamp) &&
        input.opTxExecutor.m_operatorCostFunc)
    {
        auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed));
        output.receiptMeta.operatorFee =
            input.opTxExecutor.m_operatorCostFunc(gasUsed, view.blockInfo().timestamp);
        if (feeParams.operatorFeeScalar != 0 || feeParams.operatorFeeConstant != 0)
        {
            output.receiptMeta.operatorFeeScalar = feeParams.operatorFeeScalar;
            output.receiptMeta.operatorFeeConstant = feeParams.operatorFeeConstant;
        }
    }
}

task::Task<OpStackSettlementResult> settleNormal(OpStackSettlementView view,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool)
{
    auto& ctx = view.pipelineContext();
    auto settled = finalizeNormal(ctx, view.feeSidecar(), exitKind);
    co_await ledger.refundGas(view, settled);
    if (gasPool.returnGas)
    {
        gasPool.returnGas(
            settled.gasRemaining, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return settled;
}
}  // namespace

task::Task<bool> OpStackNormalFeeSettlement::buyGas(
    OpStackSettlementView view, GasPoolHooks const& gasPool, OpStackExecutionResult& output)
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

task::Task<void> OpStackNormalFeeSettlement::completeAfterPipeline(OpStackSettlementView view,
    OpStackFeeParams const& feeParams, GasPoolHooks const& gasPool, OpStackExecutionResult& output)
{
    auto& ctx = view.pipelineContext();
    if (isNormalPreExecutionReject(ctx.exitKind))
    {
        abortNormalAfterBuyGas(ctx, gasPool, output, ctx.originalGasLimit);
        co_return;
    }

    ctx.state.commit();

    auto settled = co_await settleNormal(view, ctx.exitKind, ledger, gasPool);
    output.gasUsed = settled.gasUsed;
    projectNormalReceiptMeta(output, view, feeParams, settled);
    output.stateDiff = ctx.state.build_diff();
}

}  // namespace bcos::evm
