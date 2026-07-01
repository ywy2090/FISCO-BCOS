#include "bcos-evm/opstack/settlement/OpStackSettlement.h"
#include "bcos-evm/eth/eip/Eip1559Gate.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSidecar.h"
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
    auto const gasLimitForPool = static_cast<uint64_t>(std::max<int64_t>(0, originalGasLimit));
    gasPool.returnGas(gasLimitForPool, 0);
}

void applyPostExecuteSettlement(
    StateTransitionContext const& ctx, uint64_t floorDataGas, OpStackSettlementResult& out)
{
    auto const stateRefund =
        gas::isEip1559GasRefundEnabled(ctx.revisionConfig) ?
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) :
            uint64_t{0};
    auto const settlement =
        postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_left)), stateRefund,
            floorDataGas);
    out.gasUsed = static_cast<int64_t>(settlement.gasUsed);
    out.gasRemaining = settlement.gasRemaining;
    out.maxUsedGas = settlement.maxUsedGas;
}

void applyDepositPostExecuteSettlement(
    StateTransitionContext const& ctx, OpStackSettlementResult& out)
{
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

OpStackSettlementResult finalizeNormal(StateTransitionContext const& ctx,
    OpStackFeeSidecar const& sidecar, StateTransitionExitKind exitKind)
{
    OpStackSettlementResult out{};

    if (exitKind == StateTransitionExitKind::IntrinsicRejected ||
        exitKind == StateTransitionExitKind::GasAffordRejected)
    {
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

OpStackSettlementResult finalizeDeposit(
    StateTransitionContext& ctx, StateTransitionExitKind exitKind, evmc_status_code evmStatus)
{
    OpStackSettlementResult out{};
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
    if (ctx.state.has_checkpoint())
    {
        ctx.state.revert();
    }
    ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
    return out;
}

task::Task<OpStackSettlementResult> settleDeposit(StateTransitionContext& ctx,
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
