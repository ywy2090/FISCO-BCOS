/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Normal Eth tx fee lifecycle orchestrator.
 * @file EthNormalTxFeeCoordinator.cpp
 */

#include "bcos-evm/eth/settlement/EthNormalTxFeeCoordinator.h"
#include "bcos-evm/eth/settlement/EthFeeSettlement.h"

namespace bcos::evm
{

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

    // ① pre-exec reject（intrinsic / gas-afford）→ 单层 revert 撤销 buyGas（ADR-025）
    if (isEthPreExecutionReject(ctx.exitKind))
    {
        abortEthAfterBuyGas(ctx, output, ctx.originalGasLimit);
        co_return;
    }

    // ② 其它（SUCCESS / REVERT / vmerr / included-vmerr / ExceptionHandled）：
    //    顶层帧的 commit/revert 已由 kernel 完成（spec §1.3）——
    //    fee 层【不 revert、不 commit】，一律 refund（gas 照收 + tip）。
    auto const settled = finalizeEthNormal(
        ctx, ctx.exitKind, output.gasSettlementSnapshot, output.topLevelIncludedTxVmError);
    co_await ledger.refundGas(view, settled);

    output.gasUsed = settled.gasUsed;
    output.effectiveGasPrice = view.sidecar.effectiveGasPrice;
    if (output.effectiveGasPrice != 0)
        output.gasPriceStr = "0x" + output.effectiveGasPrice.str(256, std::ios_base::hex);
    output.stateDiff = ctx.state.build_diff();
}

}  // namespace bcos::evm
