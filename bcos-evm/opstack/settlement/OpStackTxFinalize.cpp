#include "bcos-evm/opstack/settlement/OpStackTxFinalize.h"
#include "bcos-evm/eth/eip/Eip1559Gate.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/OpStackPostExecuteGas.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSidecar.h"
#include "bcos-task/Task.h"
#include "eth/kernel/EVMCResult.h"
#include "eth/kernel/state-transition/StateTransitionContext.h"
#include "eth/state/State.hpp"
#include "eth/state/StateDiff.hpp"
#include <algorithm>

namespace bcos::evm
{
namespace
{
void releaseGasPoolFullLimit(GasPoolHooks const& gasPool, int64_t originalGasLimit)
{
    if (!gasPool.returnGas)
    {
        return;
    }
    // Abort path: return entire gas limit, zero consumed.
    auto const gasLimitForPool = static_cast<uint64_t>(std::max<int64_t>(0, originalGasLimit));
    gasPool.returnGas(gasLimitForPool, 0);
}

void applyPostExecuteSettlement(
    StateTransitionContext const& ctx, uint64_t floorDataGas, OpStackTxFinalizeResult& out)
{
    auto const stateRefund =
        gas::isEip1559GasRefundEnabled(ctx.revisionConfig) ?
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) :
            uint64_t{0};
    // Delegates to postExecuteGasSettlement — see OpStackPostExecuteGas.h formula block.
    auto const settlement =
        postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_left)), stateRefund,
            floorDataGas);
    out.gasUsed = static_cast<int64_t>(settlement.gasUsed);
    out.gasRemaining = settlement.gasRemaining;
    out.maxUsedGas = settlement.maxUsedGas;
}

void applyDepositPostExecuteSettlement(
    StateTransitionContext const& ctx, OpStackTxFinalizeResult& out)
{
    // Deposits have no Regolith floorDataGas charge.
    applyPostExecuteSettlement(ctx, 0, out);
}
}  // namespace

bool isNormalPreExecutionReject(StateTransitionExitKind exitKind) noexcept
{
    return exitKind == StateTransitionExitKind::IntrinsicRejected ||
           exitKind == StateTransitionExitKind::GasAffordRejected;
}

void abortNormalAfterBuyGas(StateTransitionContext& ctx, GasPoolHooks const& gasPool,
    OpStackMessageResult& output, int64_t originalGasLimit)
{
    if (ctx.state.has_checkpoint())
    {
        ctx.state.revert();
    }
    releaseGasPoolFullLimit(gasPool, originalGasLimit);
    output.gasUsed = 0;
    output.stateDiff = ctx.state.build_diff();
}

OpStackTxFinalizeResult finalizeNormal(StateTransitionContext const& ctx,
    OpStackFeeSidecar const& sidecar, StateTransitionExitKind exitKind)
{
    OpStackTxFinalizeResult out{};

    if (exitKind == StateTransitionExitKind::IntrinsicRejected ||
        exitKind == StateTransitionExitKind::GasAffordRejected)
    {
        // Pre-execution reject: no gas consumed; full limit returned to block pool.
        out.gasUsed = 0;
        out.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        return out;
    }

    if (exitKind == StateTransitionExitKind::Completed ||
        exitKind == StateTransitionExitKind::RulesRejected ||
        exitKind == StateTransitionExitKind::ExceptionHandled)
    {
        applyPostExecuteSettlement(ctx, sidecar.floorDataGas, out);
        return out;
    }

    return out;
}

OpStackTxFinalizeResult finalizeDeposit(
    StateTransitionContext& ctx, StateTransitionExitKind exitKind, evmc_status_code evmStatus)
{
    OpStackTxFinalizeResult out{};
    auto const sender = ctx.message.sender;

    if (exitKind == StateTransitionExitKind::Completed && evmStatus == EVMC_SUCCESS)
    {
        applyDepositPostExecuteSettlement(ctx, out);
        ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
        ctx.state.commit();
        return out;
    }

    if (exitKind == StateTransitionExitKind::Completed)
    {
        // Reverted deposit: discard execution state but always advance depositor nonce (op-geth).
        applyDepositPostExecuteSettlement(ctx, out);
        if (ctx.state.has_checkpoint())
        {
            ctx.state.revert();
        }
        ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
        return out;
    }

    out.gasUsed = std::max<int64_t>(0, ctx.originalGasLimit);
    out.gasRemaining = 0;
    // Entry failure (intrinsic gas / validation): charge full gas limit, revert, bump nonce.
    if (ctx.state.has_checkpoint())
    {
        ctx.state.revert();
    }
    ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
    return out;
}

task::Task<OpStackTxFinalizeResult> settleDeposit(StateTransitionContext& ctx,
    StateTransitionExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool)
{
    auto settled = finalizeDeposit(ctx, exitKind, evmStatus);
    if (gasPool.returnGas)
    {
        gasPool.returnGas(
            settled.gasRemaining, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return settled;
}

}  // namespace bcos::evm
