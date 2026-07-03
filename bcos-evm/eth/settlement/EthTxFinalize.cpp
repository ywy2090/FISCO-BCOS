/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Post-execution gas metering for Eth normal txs.
 * @file EthTxFinalize.cpp
 */

#include "bcos-evm/eth/settlement/EthTxFinalize.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include <algorithm>

namespace bcos::evm
{
bool isEthPreExecutionReject(StateTransitionExitKind exitKind) noexcept
{
    return exitKind == StateTransitionExitKind::IntrinsicRejected ||
           exitKind == StateTransitionExitKind::GasAffordRejected;
}

void abortEthAfterBuyGas(
    StateTransitionContext& ctx, EthMessageResult& output, int64_t /*originalGasLimit*/)
{
    if (ctx.state.has_checkpoint())
    {
        ctx.state.revert();
    }
    output.gasUsed = 0;
    output.stateDiff = ctx.state.build_diff();
}

EthTxFinalizeResult finalizeEthNormal(StateTransitionContext const& ctx,
    StateTransitionExitKind exitKind, gas::TxGasSettlementContext const& snapshot,
    bool /*topLevelIncludedTxVmError*/)
{
    // topLevelIncludedTxVmError retained for ADR-015 GAP-TE-002; not used in current TE path.
    EthTxFinalizeResult out{};
    if (isEthPreExecutionReject(exitKind))
    {
        out.gasUsed = 0;
        out.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        return out;
    }

    if (exitKind == StateTransitionExitKind::Completed ||
        exitKind == StateTransitionExitKind::ExceptionHandled)
    {
        auto const& evmcResult = ctx.evmcResult;
        // ADR-005: snapshot.gasLimit>0 implies Eip7623 mode; no isWeb3 in eth layer.
        if (ctx.revisionConfig.eip7623 && snapshot.gasLimit > 0)
        {
            out.gasUsed = gas::settleTopLevelTransactionGas(ctx.originalGasLimit,
                evmcResult.gas_left, snapshot.evmGasRefund,
                ctx.revisionConfig.calldata_floor_per_token, snapshot.calldata);
        }
        else
        {
            out.gasUsed = ctx.originalGasLimit - evmcResult.gas_left;
        }
        out.gasRemaining =
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit - out.gasUsed));
    }
    return out;
}
}  // namespace bcos::evm
