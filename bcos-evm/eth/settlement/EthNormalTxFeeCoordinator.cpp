/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Normal Eth tx fee lifecycle orchestrator.
 * @file EthNormalTxFeeCoordinator.cpp
 */

#include "bcos-evm/eth/settlement/EthNormalTxFeeCoordinator.h"
#include "bcos-evm/eth/gas/PostExecuteGasMetering.h"
#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
#include "bcos-task/Task.h"
#include "eth/apply/ApplyEthMessage.h"
#include "eth/core/RevisionConfig.h"
#include "eth/gas/GasSettlementTypes.h"
#include "eth/kernel/EVMCResult.h"
#include "eth/kernel/state-transition/StateTransitionContext.h"
#include "eth/settlement/EthFeeSidecar.h"
#include "eth/settlement/EthSettlementProjection.h"
#include "eth/state/State.hpp"
#include "eth/state/StateDiff.hpp"
#include <stdint.h>
#include <string>
#include <utility>

namespace bcos::evm
{
namespace
{
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
}  // namespace

task::Task<bool> EthNormalTxFeeCoordinator::buyGas(
    EthSettlementProjection view, EthMessageResult& output)
{
    auto& ctx = view.pipelineContext();
    if (!co_await ledger.buyGas(view))
    {
        // penalty 失败：【不 revert】—— 保留 penalty diff（spec §6 / §5.5）。
        // 对齐现 EthTxFeeSettlement::buyGas：gasUsed = penalty / effectiveGasPrice。
        output.evmcResult = std::move(ctx.evmcResult);
        output.effectiveGasPrice = view.sidecar.effectiveGasPrice;
        output.gasUsed = view.sidecar.penaltyGasUsed;
        if (output.effectiveGasPrice != 0)
            output.gasPriceStr = "0x" + output.effectiveGasPrice.str(256, std::ios_base::hex);
        co_return false;
    }
    co_return true;
}

task::Task<void> EthNormalTxFeeCoordinator::completeAfterPipeline(
    EthSettlementProjection view, EthMessageResult& output)
{
    auto& ctx = view.pipelineContext();

    // ① pre-exec reject (intrinsic / gas-afford / entry rules) → revert buyGas (geth/GST: no state
    // change on TransactionException expectException paths).
    if (gas::isPreExecutionGasReject(ctx.exitKind) || gas::isEthRulesEntryRejectAbort(ctx.exitKind))
    {
        abortEthAfterBuyGas(ctx, output, ctx.originalGasLimit);
        co_return;
    }

    // ② 其它（SUCCESS / REVERT / vmerr / included-vmerr / ExceptionHandled）：
    //    顶层帧的 commit/revert 已由 kernel 完成（spec §1.3）——
    //    fee 层【不 revert、不 commit】，一律 refund（gas 照收 + tip）。
    auto const settled = gas::meterPostExecuteGas(ctx.originalGasLimit, ctx.exitKind,
        ctx.revisionConfig.eip7623, ctx.revisionConfig.calldata_floor_per_token,
        ctx.evmcResult.gas_left, output.gasSettlementSnapshot, ctx.revisionConfig.revision);
    co_await ledger.refundGas(view, settled);

    output.gasUsed = settled.gasUsed;
    output.effectiveGasPrice = view.sidecar.effectiveGasPrice;
    if (output.effectiveGasPrice != 0)
        output.gasPriceStr = "0x" + output.effectiveGasPrice.str(256, std::ios_base::hex);
    output.stateDiff = ctx.state.build_diff();
}

}  // namespace bcos::evm
