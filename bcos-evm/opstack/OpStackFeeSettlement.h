#pragma once
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackFeeSidecar.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm
{
#define OP_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_TX_EXECUTOR")

struct OpStackSettlementResult;

struct OpStackFeeSettlement
{
    std::function<u256(const RollupCostData&, uint64_t blockTime)> m_l1CostFunc;
    std::function<u256(uint64_t gasLimit, uint64_t blockTime)> m_operatorCostFunc;
    evmc_address m_baseFeeRecipient = OP_BASE_FEE_RECIPIENT;
    evmc_address m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    evmc_address m_operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;

    task::Task<bool> buyGas(OpStackSettlementFacade view);
    task::Task<OpStackPostSettlementPlan> refundGas(
        OpStackSettlementFacade& view, OpStackSettlementResult const& settled);
};

}  // namespace bcos::evm
