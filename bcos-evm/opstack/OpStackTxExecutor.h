#pragma once
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <algorithm>
#include <functional>
#include <optional>

namespace bcos::evm
{
#define OP_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_TX_EXECUTOR")

// RollupCostData — caches per-transaction data for L1 cost computation.
// Mirrors op-geth types.RollupCostData (core/types/rollup_cost.go).
struct RollupCostData
{
    bytes cachedData;  // Serialized tx data for L1 cost function
};

struct OpStackTxExecutor
{
    // Injected at construction — mirrors op-geth BlockContext:
    //   L1CostFunc:      st.evm.Context.L1CostFunc(...)
    //   OperatorCostFunc: st.evm.Context.OperatorCostFunc(...)
    //   L1FeeRecipient:  params.OptimismL1FeeRecipient
    //   OperatorFeeRecipient: params.OptimismOperatorFeeRecipient

    std::function<u256(const RollupCostData&, uint64_t blockTime)> m_l1CostFunc;
    std::function<u256(uint64_t gasLimit, uint64_t blockTime)> m_operatorCostFunc;
    evmc_address m_l1FeeRecipient = {};
    evmc_address m_operatorFeeRecipient = {};
    bool m_isIsthmus = false;  // Operator fee active from Isthmus fork

    // Minimal per-transaction state used by OP gas accounting.
    struct OpStackTxExecutionData
    {
        bool m_call{false};
        bool m_isDepositTx{false};
        state::State* m_state{nullptr};
        evmc_message m_message{};
        bcos::u256 m_gasPrice{0};
        int64_t m_gasLimit{0};
        int64_t m_gasUsed{0};
        state::BlockInfo m_blockInfo{};
        std::optional<RollupCostData> m_rollupCostData;
        std::optional<EVMCResult> m_evmcResult;
    };

    // ── buyGas ──────────────────────────────────────────────────────────
    // op-geth buyGas() (state_transition.go:282-343):
    //   mgval = gasLimit * gasPrice
    //   if !deposit:
    //     l1Cost = L1CostFunc(RollupCostData, blockTime)  → mgval += l1Cost
    //     operatorCost = OperatorCostFunc(gasLimit, time) → mgval += operatorCost
    //   sender -= mgval
    task::Task<bool> buyGas(OpStackTxExecutionData& data)
    {
        if (data.m_call || data.m_state == nullptr)
            co_return true;

        // Deposit transactions: gas is free (op-geth preCheck: "no refunds!")
        if (data.m_isDepositTx)
            co_return true;

        const auto gasPrice = data.m_gasPrice;
        if (gasPrice == 0)
            co_return true;
        if (data.m_gasLimit <= 0)
            co_return true;

        auto mgval = u256(data.m_gasLimit) * gasPrice;
        auto l1Cost = u256{0};
        auto operatorCost = u256{0};
        uint64_t blockTime = data.m_blockInfo.timestamp;

        // OP-Stack: add L1 data fee (op-geth:289-292)
        if (m_l1CostFunc && data.m_rollupCostData)
        {
            l1Cost = m_l1CostFunc(*data.m_rollupCostData, blockTime);
            mgval += l1Cost;
        }

        // OP-Stack: add operator fee (op-geth:294-297, Isthmus fork)
        if (m_isIsthmus && m_operatorCostFunc)
        {
            operatorCost = m_operatorCostFunc(data.m_gasLimit, blockTime);
            mgval += operatorCost;
        }

        // Balance check: gas + L1 + operator + value (op-geth:299-310)
        const auto txValue = state::fromEvmC(data.m_message.value);
        auto balanceCheck = mgval + txValue;
        auto senderBalance = data.m_state->get_balance(data.m_message.sender);

        if (senderBalance < balanceCheck)
        {
            OP_TX_EXECUTOR_LOG(ERROR)
                << "buyGas: insufficient balance" << LOG_KV("balance", senderBalance)
                << LOG_KV("mgval", mgval) << LOG_KV("l1Cost", l1Cost)
                << LOG_KV("operatorCost", operatorCost) << LOG_KV("txValue", txValue);

            evmc_result failResult{};
            failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
            failResult.gas_left = 0;
            data.m_evmcResult.emplace(
                EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash));
            co_return false;
        }

        // Deduct all-in-one (op-geth:342)
        data.m_state->set_balance(data.m_message.sender, senderBalance - mgval);
        co_return true;
    }

    // ── refundGas ────────────────────────────────────────────────────────
    // op-geth post-execution (state_transition.go:715-733):
    //   coinbase += gasUsed * tip (standard)
    //   baseFeeRecipient += gasUsed * baseFee (standard)
    //   L1FeeRecipient += l1Cost            (OP only)
    //   OperatorFeeRecipient += operatorCost (OP Isthmus only)
    //   refundIsthmusOperatorCost()         (refund unused operator cost)

    task::Task<void> refundGas(OpStackTxExecutionData& data)
    {
        if (data.m_call || data.m_state == nullptr)
            co_return;
        if (data.m_isDepositTx)
            co_return;

        const auto gasPrice = data.m_gasPrice;
        if (gasPrice == 0)
            co_return;

        if (!data.m_evmcResult.has_value())
            co_return;
        auto& evmcResult = *data.m_evmcResult;

        // On EVM failure, rollback EVM state changes (op-geth behavior)
        if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
        {
            co_return;
        }

        // Standard refund: unused gas (op-geth refundGas)
        const int64_t refundGasUnits = std::max<int64_t>(0, data.m_gasLimit - data.m_gasUsed);
        if (refundGasUnits > 0)
        {
            auto refund = u256(refundGasUnits) * gasPrice;
            auto balance = data.m_state->get_balance(data.m_message.sender);
            data.m_state->set_balance(data.m_message.sender, balance + refund);
        }

        // OP-Stack: L1 fee to L1FeeRecipient (op-geth:720-725)
        if (m_l1CostFunc && data.m_rollupCostData)
        {
            uint64_t blockTime = data.m_blockInfo.timestamp;
            auto l1Cost = m_l1CostFunc(*data.m_rollupCostData, blockTime);
            if (l1Cost > 0)
            {
                auto balance = data.m_state->get_balance(m_l1FeeRecipient);
                data.m_state->set_balance(m_l1FeeRecipient, balance + l1Cost);
            }
        }

        // OP-Stack: Operator fee to OperatorFeeRecipient (op-geth:731-732, Isthmus only)
        if (m_isIsthmus && m_operatorCostFunc)
        {
            uint64_t blockTime = data.m_blockInfo.timestamp;
            // Operator cost is based on actual gasUsed, not gasLimit (op-geth:731)
            auto operatorFee = m_operatorCostFunc(data.m_gasUsed, blockTime);
            if (operatorFee > 0)
            {
                auto balance = data.m_state->get_balance(m_operatorFeeRecipient);
                data.m_state->set_balance(m_operatorFeeRecipient, balance + operatorFee);
            }
        }
    }
};

}  // namespace bcos::evm
