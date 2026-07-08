/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Eth ledger fee settlement (buyGas / refundGas).
 * @file EthFeeSettlement.cpp
 *
 * See EthFeeSettlement.h for formulas. Blob sidecar: debit uses block blobBaseFee;
 * affordability uses sender blobGasFeeCap (EIP-4844).
 */

#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Common.h"
#include "eth/apply/ApplyEthMessage.h"
#include "eth/core/RevisionConfig.h"
#include "eth/gas/TxFeeSettlement.h"
#include "eth/kernel/state-transition/StateTransitionContext.h"
#include "eth/settlement/EthFeeSidecar.h"
#include "eth/settlement/EthSettlementProjection.h"
#include "eth/state/BlockInfo.hpp"
#include "eth/state/State.hpp"
#include <evmc/evmc.h>
#include <stdint.h>
#include <algorithm>
#include <utility>
#include <vector>

namespace bcos::evm
{
namespace
{
/// EIP-4844 blob charge split: actual debit (base fee) vs cap check (max fee per blob gas).
struct BlobGasPlan
{
    bcos::u256 debit{};         ///< blobGasUsed * blobBaseFee — debited at buyGas
    bcos::u256 balanceCheck{};  ///< blobGasUsed * blobGasFeeCap — affordability only
};

BlobGasPlan planBlobGas(EthSettlementProjection const& view)
{
    BlobGasPlan plan;
    auto const& hashes = view.blobVersionedHashes();
    if (hashes.empty() || !view.input.revisionConfig.eip4844)
    {
        return plan;
    }

    auto const blobGasUsed = bcos::u256(hashes.size()) * gas::BLOB_GAS_PER_BLOB;
    plan.debit = blobGasUsed * view.blockInfo().blobBaseFee;
    plan.balanceCheck = blobGasUsed * view.blobGasFeeCap();
    return plan;
}
}  // namespace

task::Task<bool> EthFeeSettlement::buyGas(EthSettlementProjection view)
{
    auto& ctx = view.pipelineContext();
    auto& sidecar = view.sidecar;

    // eth_call and zero-gas-limit paths: no balance movement.
    if (view.isCall())
    {
        co_return true;
    }
    if (ctx.originalGasLimit <= 0)
    {
        co_return true;
    }

    // Phase 1: resolve effectiveGasPrice and preDebitAmount (state-free).
    auto const feeInputs = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
        gas::FeeCapsView{view.gasPriceLegacy(), view.gasTipCap(), view.gasFeeCap(),
            view.web3TypedTxKind(), view.hasExplicitFeeCaps()},
        ctx.originalGasLimit);
    auto const plan = gas::planPreExecution(feeInputs);
    sidecar.effectiveGasPrice = plan.effectiveGasPrice;
    auto const blobPlan = planBlobGas(view);
    if (sidecar.effectiveGasPrice == 0 && blobPlan.debit == 0)
    {
        co_return true;
    }

    // Phase 2: overflow guard on gasLimit * effectiveGasPrice.
    if (sidecar.effectiveGasPrice > 0 && ctx.originalGasLimit > 0)
    {
        bcos::u256 preDebitCheck{};
        if (gas::mulU256Overflow(
                bcos::u256(ctx.originalGasLimit), sidecar.effectiveGasPrice, preDebitCheck))
        {
            ctx.evmcResult = EVMCResult(evmc_result{.status_code = EVMC_INSUFFICIENT_BALANCE},
                protocol::TransactionStatus::NotEnoughCash);
            co_return false;
        }
    }

    // Phase 3: affordability — maxBalanceDebit (EIP-1559) + blob cap + value.
    auto blobBalanceCheck = blobPlan.balanceCheck;
    if (blobBalanceCheck == 0 && blobPlan.debit > 0)
    {
        blobBalanceCheck = blobPlan.debit;
    }
    auto const totalRequired = plan.maxBalanceDebit + blobBalanceCheck + view.txValue();
    auto const senderBalance = ctx.state.get_balance(ctx.message.sender);
    if (senderBalance < totalRequired)
    {
        // geth buyGas penalty: charge up to TX_BASE_GAS * effectiveGasPrice, then fail tx.
        auto const intrinsicCost = bcos::u256(gas::TX_BASE_GAS) * sidecar.effectiveGasPrice;
        auto const penalty = std::min(senderBalance, intrinsicCost);
        if (penalty > 0)
        {
            ctx.state.set_balance(ctx.message.sender, senderBalance - penalty);
        }

        // effectiveGasPrice can be 0 here (a blob tx with baseFee=0 and zero fee caps still
        // reaches this branch via blobPlan.debit > 0); guard the division — no per-gas price
        // means no gas-denominated penalty.
        sidecar.penaltyGasUsed = sidecar.effectiveGasPrice > 0 ?
                                     (penalty / sidecar.effectiveGasPrice).convert_to<int64_t>() :
                                     0;

        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash);
        co_return false;
    }

    // Phase 4: full pre-debit (execution gas + blob base fee).
    ctx.state.set_balance(ctx.message.sender, senderBalance - plan.preDebitAmount - blobPlan.debit);
    co_return true;
}

task::Task<gas::FeeSettlementPlan> EthFeeSettlement::refundGas(
    EthSettlementProjection& view, gas::PostExecuteGasResult const& settled)
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

    // Credit sender unused gas; credit coinbase priority fee. Base fee is burned (no entry).
    ctx.state.add_balance(ctx.message.sender, plan.unusedRefund);
    ctx.state.add_balance(view.blockInfo().coinbase, plan.coinbaseTip);

    co_return plan;
}
}  // namespace bcos::evm
