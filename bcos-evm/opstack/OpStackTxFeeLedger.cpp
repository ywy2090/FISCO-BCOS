#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/pipeline/FeeInputsProjection.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/fee/OpStackPreDebitInputs.h"
#include "bcos-evm/opstack/fee/OpStackPreDebitPlan.h"

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

task::Task<bool> OpStackTxFeeLedger::buyGas(OpStackSettlementView view)
{
    auto& ctx = view.pipelineContext();
    auto& sidecar = view.mutableSidecar();

    if (view.isCall() || view.isDeposit())
    {
        co_return true;
    }
    if (ctx.originalGasLimit <= 0)
    {
        co_return true;
    }

    OpStackFeeHooks hooks{};
    if (m_l1CostFunc)
    {
        hooks.l1CostFunc = &m_l1CostFunc;
    }
    if (m_operatorCostFunc)
    {
        hooks.operatorCostFunc = &m_operatorCostFunc;
    }

    auto const plan = planOpStackPreDebit(toOpStackPreDebitInputs(view), hooks);

    sidecar.effectiveGasPrice = plan.sidecar.effectiveGasPrice;
    sidecar.baseFee = plan.sidecar.baseFee;
    sidecar.l1CostCharged = plan.sidecar.l1CostCharged;
    sidecar.operatorCostLimit = plan.sidecar.operatorCostLimit;

    auto const senderBalance = ctx.state.get_balance(ctx.message.sender);
    if (senderBalance < plan.balanceCheck)
    {
        OP_TX_EXECUTOR_LOG(ERROR) << "buyGas: insufficient balance"
                                  << LOG_KV("balance", senderBalance)
                                  << LOG_KV("required", plan.balanceCheck);
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash);
        co_return false;
    }

    ctx.state.set_balance(ctx.message.sender, senderBalance - plan.totalDebit);
    co_return true;
}

task::Task<void> OpStackTxFeeLedger::refundIsthmusOperatorCost(
    OpStackSettlementView& view, uint64_t gasUsed)
{
    if (!m_operatorCostFunc)
    {
        co_return;
    }

    auto& sidecar = view.feeSidecar();
    auto const usedCost = m_operatorCostFunc(gasUsed, view.blockInfo().timestamp);
    if (usedCost >= sidecar.operatorCostLimit)
    {
        co_return;
    }

    addBalance(view.pipelineContext().state, view.pipelineContext().message.sender,
        sidecar.operatorCostLimit - usedCost);
}

task::Task<void> OpStackTxFeeLedger::refundGas(
    OpStackSettlementView& view, OpStackSettlementResult const& settled)
{
    if (view.isDeposit())
    {
        co_return;
    }

    if (view.isCall() && view.skipTransactionChecks() && view.noBaseFee() &&
        view.gasFeeCap() == 0 && view.gasTipCap() == 0)
    {
        co_return;
    }

    auto& ctx = view.pipelineContext();
    auto& state = ctx.state;
    auto const& sidecar = view.feeSidecar();
    auto const gasRemaining = settled.gasRemaining;
    auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed));

    auto const feeInputs = gas::toFeeInputs(ctx.revisionConfig, view.blockInfo(),
        gas::FeeCapsView{ctx.gasPrice, view.gasTipCap(), view.gasFeeCap(), view.web3TypedTxKind(),
            view.hasGasFeeCap()},
        ctx.originalGasLimit);
    auto const plan = gas::planPostExecution(
        feeInputs, static_cast<int64_t>(gasUsed), static_cast<int64_t>(gasRemaining));

    addBalance(state, ctx.message.sender, plan.unusedRefund);
    addBalance(state, view.blockInfo().coinbase, plan.coinbaseTip);
    addBalance(state, m_baseFeeRecipient, plan.baseFeeAmount);
    addBalance(state, m_l1FeeRecipient, sidecar.l1CostCharged);

    co_await refundIsthmusOperatorCost(view, gasUsed);
    if (m_operatorCostFunc)
    {
        auto const operatorFee = m_operatorCostFunc(gasUsed, view.blockInfo().timestamp);
        addBalance(state, m_operatorFeeRecipient, operatorFee);
    }
}
}  // namespace bcos::evm
