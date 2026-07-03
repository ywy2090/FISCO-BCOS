/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief State-based Eth fee settlement (buyGas / refundGas).
 * @file EthFeeSettlement.cpp
 */

#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-evm/eth/settlement/EthTxFinalize.h"
#include <algorithm>

namespace bcos::evm
{
namespace
{
void addBalance(state::State& state, evmc_address const& address, bcos::u256 const& delta)
{
    if (delta == 0)
    {
        return;
    }
    state.set_balance(address, state.get_balance(address) + delta);
}
}  // namespace

task::Task<bool> EthFeeSettlement::buyGas(EthSettlementProjection view)
{
    auto& ctx = view.pipelineContext();
    auto& sidecar = view.sidecar;

    if (view.isCall())
    {
        co_return true;
    }
    if (ctx.originalGasLimit <= 0)
    {
        co_return true;
    }

    auto const feeInputs = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
        gas::FeeCapsView{view.gasPriceLegacy(), view.gasTipCap(), view.gasFeeCap(),
            view.web3TypedTxKind(), view.hasExplicitFeeCaps()},
        ctx.originalGasLimit);
    auto const plan = gas::planPreExecution(feeInputs);
    sidecar.effectiveGasPrice = plan.effectiveGasPrice;
    if (sidecar.effectiveGasPrice == 0)
    {
        co_return true;
    }

    auto const totalRequired = plan.maxBalanceDebit + view.txValue();
    auto const senderBalance = ctx.state.get_balance(ctx.message.sender);
    if (senderBalance < totalRequired)
    {
        auto const intrinsicCost = bcos::u256(gas::TX_BASE_GAS) * sidecar.effectiveGasPrice;
        auto const penalty = std::min(senderBalance, intrinsicCost);
        if (penalty > 0)
        {
            ctx.state.set_balance(ctx.message.sender, senderBalance - penalty);
        }

        sidecar.penaltyGasUsed = (penalty / sidecar.effectiveGasPrice).convert_to<int64_t>();

        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash);
        co_return false;
    }

    ctx.state.set_balance(ctx.message.sender, senderBalance - plan.preDebitAmount);
    co_return true;
}

task::Task<gas::FeeSettlementPlan> EthFeeSettlement::refundGas(
    EthSettlementProjection& view, EthTxFinalizeResult const& settled)
{
    auto& ctx = view.pipelineContext();
    if (view.isCall() || view.sidecar.effectiveGasPrice == 0)
    {
        co_return gas::FeeSettlementPlan{};
    }

    auto const feeInputs = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
        gas::FeeCapsView{view.gasPriceLegacy(), view.gasTipCap(), view.gasFeeCap(),
            view.web3TypedTxKind(), view.hasExplicitFeeCaps()},
        ctx.originalGasLimit);

    auto const plan = gas::planPostExecution(
        feeInputs, settled.gasUsed, static_cast<int64_t>(settled.gasRemaining));

    addBalance(ctx.state, ctx.message.sender, plan.unusedRefund);
    addBalance(ctx.state, view.blockInfo().coinbase, plan.coinbaseTip);
    // baseFeeAmount implicitly burned on Eth — no credit

    co_return plan;
}
}  // namespace bcos::evm
