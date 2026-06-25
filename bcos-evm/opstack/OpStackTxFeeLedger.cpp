#include "bcos-evm/opstack/OpStackTxFeeLedger.h"

#include <algorithm>

namespace bcos::evm
{
namespace
{
void addBalance(state::State& state, evmc_address const& address, u256 const& delta)
{
    if (delta == 0)
    {
        return;
    }
    state.set_balance(address, state.get_balance(address) + delta);
}
}  // namespace

u256 resolveEffectiveGasPrice(u256 const& gasTipCap, u256 const& gasFeeCap, u256 const& baseFee)
{
    return std::min(gasTipCap + baseFee, gasFeeCap);
}

task::Task<bool> OpStackTxFeeLedger::buyGas(TxPipelineContext& ctx, OpStackFeeContext& feeCtx)
{
    if (feeCtx.m_call || feeCtx.m_isDepositTx)
    {
        co_return true;
    }
    if (ctx.originalGasLimit <= 0)
    {
        co_return true;
    }

    auto const baseFee = feeCtx.m_blockInfo.baseFee;
    feeCtx.m_baseFee = baseFee;

    auto gasTipCap = feeCtx.m_gasTipCap;
    auto gasFeeCap = feeCtx.m_gasFeeCap;
    if (!feeCtx.m_hasGasFeeCap)
    {
        gasTipCap = feeCtx.m_gasPrice;
        gasFeeCap = feeCtx.m_gasPrice;
    }

    feeCtx.m_effectiveGasPrice = resolveEffectiveGasPrice(gasTipCap, gasFeeCap, baseFee);

    auto const gasLimit = u256(ctx.originalGasLimit);
    auto mgval = gasLimit * feeCtx.m_effectiveGasPrice;
    uint64_t const blockTime = feeCtx.m_blockInfo.timestamp;

    feeCtx.m_l1CostCharged = 0;
    if (m_l1CostFunc && feeCtx.m_rollupCostData)
    {
        feeCtx.m_l1CostCharged = m_l1CostFunc(*feeCtx.m_rollupCostData, blockTime);
        mgval += feeCtx.m_l1CostCharged;
    }

    feeCtx.m_operatorCostLimit = 0;
    if (m_operatorCostFunc)
    {
        feeCtx.m_operatorCostLimit =
            m_operatorCostFunc(static_cast<uint64_t>(ctx.originalGasLimit), blockTime);
        mgval += feeCtx.m_operatorCostLimit;
    }

    u256 blobGasUsed{0};
    u256 blobBalanceCheck{0};
    if (!feeCtx.m_blobVersionedHashes.empty())
    {
        blobGasUsed = u256(feeCtx.m_blobVersionedHashes.size()) * OP_BLOB_GAS_PER_BLOB;
        auto const blobCost = blobGasUsed * feeCtx.m_blockInfo.blobBaseFee;
        mgval += blobCost;
        blobBalanceCheck = blobGasUsed * feeCtx.m_blobGasFeeCap;
    }

    auto const txValue = state::fromEvmC(ctx.message.value);
    auto balanceCheck = mgval + txValue;
    if (feeCtx.m_hasGasFeeCap)
    {
        balanceCheck = gasLimit * gasFeeCap + feeCtx.m_l1CostCharged + feeCtx.m_operatorCostLimit +
                       blobBalanceCheck + txValue;
    }

    auto const senderBalance = ctx.state.get_balance(ctx.message.sender);
    if (senderBalance < balanceCheck)
    {
        OP_TX_EXECUTOR_LOG(ERROR) << "buyGas: insufficient balance"
                                  << LOG_KV("balance", senderBalance)
                                  << LOG_KV("required", balanceCheck);
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        feeCtx.m_evmcResult.emplace(
            EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash));
        co_return false;
    }

    ctx.state.set_balance(ctx.message.sender, senderBalance - mgval);
    co_return true;
}

task::Task<void> OpStackTxFeeLedger::refundIsthmusOperatorCost(
    TxPipelineContext& ctx, OpStackFeeContext& feeCtx)
{
    if (!m_operatorCostFunc)
    {
        co_return;
    }

    auto const usedGas = static_cast<uint64_t>(std::max<int64_t>(0, feeCtx.m_gasUsed));
    auto const usedCost = m_operatorCostFunc(usedGas, feeCtx.m_blockInfo.timestamp);
    if (usedCost >= feeCtx.m_operatorCostLimit)
    {
        co_return;
    }

    addBalance(ctx.state, ctx.message.sender, feeCtx.m_operatorCostLimit - usedCost);
}

task::Task<void> OpStackTxFeeLedger::refundGas(TxPipelineContext& ctx, OpStackFeeContext& feeCtx)
{
    if (feeCtx.m_isDepositTx)
    {
        co_return;
    }

    if (feeCtx.m_call && feeCtx.m_skipTransactionChecks && feeCtx.m_noBaseFee &&
        feeCtx.m_gasFeeCap == 0 && feeCtx.m_gasTipCap == 0)
    {
        co_return;
    }

    auto& state = ctx.state;
    auto const gasRemaining = feeCtx.m_gasRemaining;
    auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, feeCtx.m_gasUsed));

    addBalance(state, ctx.message.sender, u256(gasRemaining) * feeCtx.m_effectiveGasPrice);

    auto const effectiveTip = (feeCtx.m_effectiveGasPrice > feeCtx.m_baseFee) ?
                                  (feeCtx.m_effectiveGasPrice - feeCtx.m_baseFee) :
                                  u256(0);
    addBalance(state, feeCtx.m_blockInfo.coinbase, u256(gasUsed) * effectiveTip);
    addBalance(state, m_baseFeeRecipient, u256(gasUsed) * feeCtx.m_baseFee);
    addBalance(state, m_l1FeeRecipient, feeCtx.m_l1CostCharged);

    co_await refundIsthmusOperatorCost(ctx, feeCtx);
    if (m_operatorCostFunc)
    {
        auto const operatorFee = m_operatorCostFunc(gasUsed, feeCtx.m_blockInfo.timestamp);
        addBalance(state, m_operatorFeeRecipient, operatorFee);
    }
}
}  // namespace bcos::evm
