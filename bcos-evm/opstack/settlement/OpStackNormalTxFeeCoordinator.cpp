#include "bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/OpStackFeeParams.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/OpStackReceiptMetaProjection.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSettlement.h"
#include "bcos-task/Task.h"
#include "eth/kernel/EVMCResult.h"
#include "eth/kernel/state-transition/StateTransitionContext.h"
#include "eth/state/BlockInfo.hpp"
#include "eth/state/State.hpp"
#include "eth/state/StateDiff.hpp"
#include "opstack/settlement/OpStackSettlementProjection.h"
#include "opstack/settlement/OpStackTxFinalize.h"
#include "opstack/types/OpStackReceiptMeta.h"
#include <stdint.h>
#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace bcos::evm
{
namespace
{
struct NormalSettleOutcome
{
    OpStackTxFinalizeResult settled;
    OpStackPostSettlementPlan feePlan;
};

/// Populate OP Stack receipt metadata via fee/ projection (l1Fee, operatorFee, daFootprint).
void projectNormalReceiptMeta(OpStackMessageResult& output, OpStackSettlementProjection& view,
    OpStackFeeParams const& feeParams, OpStackPostSettlementPlan const& feePlan)
{
    projectOpStackReceiptMeta(output.receiptMeta,
        OpStackReceiptMetaProjectionInput{
            .forkSchedule = view.input.forkSchedule,
            .blockTime = static_cast<uint64_t>(view.blockInfo().timestamp),
            .hasOperatorCostFunc = view.input.opTxExecutor.m_operatorCostFunc != nullptr,
            .feeParams = feeParams,
            .feePlan = feePlan,
            .rollupCostData = view.rollupCostData(),
        });
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
    projectNormalReceiptMeta(output, view, feeParams, outcome.feePlan);
    output.stateDiff = ctx.state.build_diff();
}

}  // namespace bcos::evm
