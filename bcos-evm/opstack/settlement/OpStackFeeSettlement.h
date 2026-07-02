#pragma once

// Ledger layer: pre-debit (buyGas) and post-execution refund + fee routing.
// Aligns with op-geth StateProcessor buyGas / refundGas; uses opstack/fee/* for plan math.

#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSidecar.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementProjection.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm
{
#define OP_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_TX_EXECUTOR")

struct OpStackTxFinalizeResult;

struct OpStackFeeSettlement
{
    std::function<u256(const RollupCostData&, uint64_t blockTime)> m_l1CostFunc;
    std::function<u256(uint64_t gasLimit, uint64_t blockTime)> m_operatorCostFunc;
    // OP Stack predeploy fee recipients (OpStackConstants.h)
    evmc_address m_baseFeeRecipient = OP_BASE_FEE_RECIPIENT;
    evmc_address m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    evmc_address m_operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;

    /// Pre-execution debit: EIP-1559 + L1 + operator + blob. Skipped for call/deposit.
    task::Task<bool> buyGas(OpStackSettlementProjection view);
    /// Post-execution: unused gas refund to sender, route fees to coinbase + predeploy recipients.
    task::Task<OpStackPostSettlementPlan> refundGas(
        OpStackSettlementProjection& view, OpStackTxFinalizeResult const& settled);
};

}  // namespace bcos::evm
