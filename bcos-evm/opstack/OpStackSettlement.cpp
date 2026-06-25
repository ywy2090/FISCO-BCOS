#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include <algorithm>

namespace bcos::evm
{
namespace
{
void applyPostExecuteSettlement(
    TxPipelineContext const& ctx, OpStackFeeContext& feeCtx, OpStackSettlementResult& out)
{
    auto const stateRefund =
        ctx.revisionConfig.eip1559 ?
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_refund)) :
            uint64_t{0};
    auto const settlement =
        postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.evmcResult.gas_left)), stateRefund,
            feeCtx.m_floorDataGas);
    feeCtx.m_gasRemaining = settlement.gasRemaining;
    feeCtx.m_maxUsedGas = settlement.maxUsedGas;
    feeCtx.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);
    out.gasUsed = feeCtx.m_gasUsed;
    out.gasRemaining = settlement.gasRemaining;
    out.maxUsedGas = settlement.maxUsedGas;
}
}  // namespace

OpStackSettlementResult finalizeNormal(TxPipelineContext const& ctx, OpStackFeeContext& feeCtx,
    TxPipelineExitKind exitKind, OpStackTxFeeLedger& /*ledger*/, GasPoolHooks const& /*gasPool*/)
{
    OpStackSettlementResult out{};

    if (exitKind == TxPipelineExitKind::IntrinsicRejected ||
        exitKind == TxPipelineExitKind::GasAffordRejected)
    {
        feeCtx.m_gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        feeCtx.m_gasUsed = 0;
        out.gasUsed = 0;
        out.gasRemaining = feeCtx.m_gasRemaining;
        return out;
    }

    if (exitKind == TxPipelineExitKind::Completed ||
        exitKind == TxPipelineExitKind::RulesRejected ||
        exitKind == TxPipelineExitKind::ExceptionHandled)
    {
        applyPostExecuteSettlement(ctx, feeCtx, out);
        return out;
    }

    return out;
}

}  // namespace bcos::evm
