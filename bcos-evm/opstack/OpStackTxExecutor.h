#pragma once
#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/RollupCost.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm
{
#define OP_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_TX_EXECUTOR")

u256 resolveEffectiveGasPrice(u256 const& gasTipCap, u256 const& gasFeeCap, u256 const& baseFee);

struct OpStackTxExecutor
{
    std::function<u256(const RollupCostData&, uint64_t blockTime)> m_l1CostFunc;
    std::function<u256(uint64_t gasLimit, uint64_t blockTime)> m_operatorCostFunc;
    evmc_address m_baseFeeRecipient = OP_BASE_FEE_RECIPIENT;
    evmc_address m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    evmc_address m_operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;
    bool m_isIsthmus = false;  // Operator fee active from Isthmus fork

    struct OpStackTxExecutionData
    {
        bool m_call{false};
        bool m_isDepositTx{false};
        bool m_skipNonceChecks{false};
        bool m_skipTransactionChecks{false};
        bool m_noBaseFee{false};
        state::State* m_state{nullptr};
        evmc_message m_message{};
        bcos::u256 m_gasPrice{0};
        bcos::u256 m_gasTipCap{0};
        bcos::u256 m_gasFeeCap{0};
        bool m_hasGasFeeCap{false};
        int64_t m_gasLimit{0};
        int64_t m_gasUsed{0};
        uint64_t m_gasRemaining{0};
        uint64_t m_maxUsedGas{0};
        state::BlockInfo m_blockInfo{};
        bcos::u256 m_l1CostCharged{0};
        bcos::u256 m_operatorCostLimit{0};
        bcos::u256 m_effectiveGasPrice{0};
        bcos::u256 m_baseFee{0};
        uint64_t m_floorDataGas{0};
        const Eip2930AccessList* m_accessList{nullptr};
        uint8_t m_web3TypedTxKind{0};
        uint64_t m_authTupleCount{0};
        std::optional<RollupCostData> m_rollupCostData;
        std::optional<EVMCResult> m_evmcResult;
    };

    task::Task<bool> buyGas(OpStackTxExecutionData& data);
    task::Task<void> refundGas(OpStackTxExecutionData& data);
    task::Task<void> refundIsthmusOperatorCost(OpStackTxExecutionData& data);
};

}  // namespace bcos::evm
