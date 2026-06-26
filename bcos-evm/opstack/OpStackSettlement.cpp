#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/eth/gas/Eip1559Access.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include <algorithm>

namespace bcos::evm
{
namespace
{
void applyPostExecuteSettlement(
    TxPipelineContext const& ctx, uint64_t floorDataGas, OpStackSettlementResult& out)
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

void applyDepositPostExecuteSettlement(TxPipelineContext const& ctx, OpStackSettlementResult& out)
{
    applyPostExecuteSettlement(ctx, 0, out);
}
}  // namespace

OpStackSettlementResult finalizeNormal(
    TxPipelineContext const& ctx, OpStackFeeContext const& feeCtx, TxPipelineExitKind exitKind)
{
    OpStackSettlementResult out{};

    if (exitKind == TxPipelineExitKind::IntrinsicRejected ||
        exitKind == TxPipelineExitKind::GasAffordRejected)
    {
        out.gasUsed = 0;
        out.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        return out;
    }

    if (exitKind == TxPipelineExitKind::Completed ||
        exitKind == TxPipelineExitKind::RulesRejected ||
        exitKind == TxPipelineExitKind::ExceptionHandled)
    {
        applyPostExecuteSettlement(ctx, feeCtx.m_floorDataGas, out);
        return out;
    }

    return out;
}

OpStackSettlementResult finalizeDeposit(
    TxPipelineContext& ctx, TxPipelineExitKind exitKind, evmc_status_code evmStatus)
{
    OpStackSettlementResult out{};
    auto const sender = ctx.message.sender;

    if (exitKind == TxPipelineExitKind::Completed && evmStatus == EVMC_SUCCESS)
    {
        applyDepositPostExecuteSettlement(ctx, out);
        ctx.state.set_nonce(sender, ctx.state.get_nonce(sender) + 1);
        ctx.state.commit();
        return out;
    }

    if (exitKind == TxPipelineExitKind::Completed)
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

task::Task<OpStackSettlementResult> settleNormal(TxPipelineContext& ctx, OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& ledger, GasPoolHooks const& gasPool)
{
    auto settled = finalizeNormal(ctx, feeCtx, exitKind);
    co_await ledger.refundGas(ctx, feeCtx, settled);
    if (gasPool.returnGas)
    {
        gasPool.returnGas(
            settled.gasRemaining, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
    }
    co_return settled;
}

task::Task<OpStackSettlementResult> settleDeposit(TxPipelineContext& ctx,
    TxPipelineExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool)
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
