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

task::Task<bool> OpStackTxFeeLedger::buyGas(OpStackTxExecutionData& data)
{
    if (data.m_call || data.m_state == nullptr || data.m_isDepositTx)
    {
        co_return true;
    }
    if (data.m_gasLimit <= 0)
    {
        co_return true;
    }

    auto const baseFee = data.m_blockInfo.baseFee;
    data.m_baseFee = baseFee;

    auto gasTipCap = data.m_gasTipCap;
    auto gasFeeCap = data.m_gasFeeCap;
    if (!data.m_hasGasFeeCap)
    {
        gasTipCap = data.m_gasPrice;
        gasFeeCap = data.m_gasPrice;
    }

    data.m_effectiveGasPrice = resolveEffectiveGasPrice(gasTipCap, gasFeeCap, baseFee);

    auto const gasLimit = u256(data.m_gasLimit);
    auto mgval = gasLimit * data.m_effectiveGasPrice;
    uint64_t const blockTime = data.m_blockInfo.timestamp;

    data.m_l1CostCharged = 0;
    if (m_l1CostFunc && data.m_rollupCostData)
    {
        data.m_l1CostCharged = m_l1CostFunc(*data.m_rollupCostData, blockTime);
        mgval += data.m_l1CostCharged;
    }

    data.m_operatorCostLimit = 0;
    if (m_operatorCostFunc)
    {
        data.m_operatorCostLimit =
            m_operatorCostFunc(static_cast<uint64_t>(data.m_gasLimit), blockTime);
        mgval += data.m_operatorCostLimit;
    }

    u256 blobGasUsed{0};
    u256 blobBalanceCheck{0};
    if (!data.m_blobVersionedHashes.empty())
    {
        blobGasUsed = u256(data.m_blobVersionedHashes.size()) * OP_BLOB_GAS_PER_BLOB;
        auto const blobCost = blobGasUsed * data.m_blockInfo.blobBaseFee;
        mgval += blobCost;
        blobBalanceCheck = blobGasUsed * data.m_blobGasFeeCap;
    }

    auto const txValue = state::fromEvmC(data.m_message.value);
    auto balanceCheck = mgval + txValue;
    if (data.m_hasGasFeeCap)
    {
        balanceCheck = gasLimit * gasFeeCap + data.m_l1CostCharged + data.m_operatorCostLimit +
                       blobBalanceCheck + txValue;
    }

    auto const senderBalance = data.m_state->get_balance(data.m_message.sender);
    if (senderBalance < balanceCheck)
    {
        OP_TX_EXECUTOR_LOG(ERROR) << "buyGas: insufficient balance"
                                  << LOG_KV("balance", senderBalance)
                                  << LOG_KV("required", balanceCheck);
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        data.m_evmcResult.emplace(
            EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash));
        co_return false;
    }

    data.m_state->set_balance(data.m_message.sender, senderBalance - mgval);
    co_return true;
}

task::Task<void> OpStackTxFeeLedger::refundIsthmusOperatorCost(OpStackTxExecutionData& data)
{
    if (data.m_state == nullptr || !m_operatorCostFunc)
    {
        co_return;
    }

    auto const usedGas = static_cast<uint64_t>(std::max<int64_t>(0, data.m_gasUsed));
    auto const usedCost = m_operatorCostFunc(usedGas, data.m_blockInfo.timestamp);
    if (usedCost >= data.m_operatorCostLimit)
    {
        co_return;
    }

    addBalance(*data.m_state, data.m_message.sender, data.m_operatorCostLimit - usedCost);
}

task::Task<void> OpStackTxFeeLedger::refundGas(OpStackTxExecutionData& data)
{
    if (data.m_state == nullptr || data.m_isDepositTx)
    {
        co_return;
    }

    if (data.m_call && data.m_skipTransactionChecks && data.m_noBaseFee && data.m_gasFeeCap == 0 &&
        data.m_gasTipCap == 0)
    {
        co_return;
    }

    auto& state = *data.m_state;
    auto const gasRemaining = data.m_gasRemaining;
    auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, data.m_gasUsed));

    addBalance(state, data.m_message.sender, u256(gasRemaining) * data.m_effectiveGasPrice);

    auto const effectiveTip = (data.m_effectiveGasPrice > data.m_baseFee) ?
                                  (data.m_effectiveGasPrice - data.m_baseFee) :
                                  u256(0);
    addBalance(state, data.m_blockInfo.coinbase, u256(gasUsed) * effectiveTip);
    addBalance(state, m_baseFeeRecipient, u256(gasUsed) * data.m_baseFee);
    addBalance(state, m_l1FeeRecipient, data.m_l1CostCharged);

    co_await refundIsthmusOperatorCost(data);
    if (m_operatorCostFunc)
    {
        auto const operatorFee = m_operatorCostFunc(gasUsed, data.m_blockInfo.timestamp);
        addBalance(state, m_operatorFeeRecipient, operatorFee);
    }
}
}  // namespace bcos::evm
