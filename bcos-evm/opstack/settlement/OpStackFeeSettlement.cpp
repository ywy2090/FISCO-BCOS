#include "bcos-evm/opstack/settlement/OpStackFeeSettlement.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementInputsMapping.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/OpStackPreDebitInputsMapping.h"
#include "bcos-evm/opstack/fee/OpStackPreDebitPlan.h"
#include "bcos-evm/opstack/settlement/OpStackTxFinalize.h"

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

task::Task<bool> OpStackFeeSettlement::buyGas(OpStackSettlementProjection view)
{
    auto& ctx = view.pipelineContext();
    auto& sidecar = view.mutableSidecar();

    if (view.isCall() || view.isDeposit())
    {
        // eth_call and system deposits do not pre-debit L1/operator fees from sender.
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

    // Persist debit snapshot for post-execution refundGas and receipt projection.
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

task::Task<OpStackPostSettlementPlan> OpStackFeeSettlement::refundGas(
    OpStackSettlementProjection& view, OpStackTxFinalizeResult const& settled)
{
    if (view.isDeposit())
    {
        // Deposits use a separate settlement path (settleDeposit); no fee routing here.
        co_return OpStackPostSettlementPlan{};
    }
    // Zero-fee eth_call (gasFeeCap=0, gasTipCap=0, noBaseFee): skip all balance mutations.
    if (view.isCall() && view.skipTransactionChecks() && view.noBaseFee() &&
        view.gasFeeCap() == 0 && view.gasTipCap() == 0)
    {
        co_return OpStackPostSettlementPlan{};
    }

    auto& ctx = view.pipelineContext();
    auto& state = ctx.state;

    OpStackFeeHooks hooks{};
    if (m_operatorCostFunc)
    {
        hooks.operatorCostFunc = &m_operatorCostFunc;
    }

    auto const plan =
        planOpStackPostSettlement(toOpStackPostSettlementInputs(view, settled), hooks);

    addBalance(state, ctx.message.sender, plan.core1559.unusedRefund + plan.senderOperatorRefund);
    addBalance(state, view.blockInfo().coinbase, plan.core1559.coinbaseTip);
    // OP Stack: base/L1/operator fees go to system predeploy recipients (not burned).
    addBalance(state, m_baseFeeRecipient, plan.core1559.baseFeeAmount);
    addBalance(state, m_l1FeeRecipient, plan.l1FeeRouted);
    if (hooks.operatorCostFunc != nullptr)
    {
        // Operator fee only routed when Isthmus+ schedule wires operatorCostFunc.
        addBalance(state, m_operatorFeeRecipient, plan.operatorFeeCharged);
    }

    co_return plan;
}
}  // namespace bcos::evm
