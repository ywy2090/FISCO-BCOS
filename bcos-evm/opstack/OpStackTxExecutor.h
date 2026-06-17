#pragma once
#include <bcos-task/Task.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <functional>
#include <optional>

namespace bcos::executor_v1
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
    using PolicyType = bcos::evm_standard::StandardEthPolicy;
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

    // ── buyGas ──────────────────────────────────────────────────────────
    // op-geth buyGas() (state_transition.go:282-343):
    //   mgval = gasLimit * gasPrice
    //   if !deposit:
    //     l1Cost = L1CostFunc(RollupCostData, blockTime)  → mgval += l1Cost
    //     operatorCost = OperatorCostFunc(gasLimit, time) → mgval += operatorCost
    //   balanceCheck = mgval (or gasFeeCap*gasLimit + l1Cost + opCost) + value
    //   sender -= mgval

    template <class Data>
    task::Task<bool> buyGas(Data& data)
    {
        if (data.m_call) co_return true;

        // Deposit transactions: gas is free (op-geth preCheck: "no refunds!")
        if (isDepositTx(data.m_transaction.get()))
            co_return true;

        const auto gasPrice = protocol::effectiveGasPrice(data.m_transaction.get());
        if (gasPrice == 0) co_return true;
        if (data.m_gasLimit <= 0) co_return true;

        auto mgval = u256(data.m_gasLimit) * gasPrice;
        auto l1Cost = u256{0};
        auto operatorCost = u256{0};
        uint64_t blockTime = data.m_blockHeader.get().timestamp();

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
        const auto txValue = u256(data.m_transaction.get().value());
        auto balanceCheck = mgval + txValue;

        auto& msg = data.m_hostContext.message();
        auto senderAccount = getAccount(data.m_hostContext, msg.sender);
        auto senderBalance = co_await senderAccount.balance();

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
            data.m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);
            co_return false;
        }

        // Deduct all-in-one (op-geth:342)
        co_await senderAccount.setBalance(senderBalance - mgval);
        data.m_afterBuyGasSavepoint = data.m_rollbackableStorage.current();
        data.m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);
        co_return true;
    }

    // ── refundGas ────────────────────────────────────────────────────────
    // op-geth post-execution (state_transition.go:715-733):
    //   coinbase += gasUsed * tip (standard)
    //   baseFeeRecipient += gasUsed * baseFee (standard)
    //   L1FeeRecipient += l1Cost            (OP only)
    //   OperatorFeeRecipient += operatorCost (OP Isthmus only)
    //   refundIsthmusOperatorCost()         (refund unused operator cost)

    template <class Data>
    task::Task<void> refundGas(Data& data)
    {
        if (data.m_call) co_return;
        if (isDepositTx(data.m_transaction.get())) co_return;

        const auto gasPrice = protocol::effectiveGasPrice(data.m_transaction.get());
        if (gasPrice == 0) co_return;

        auto& evmcResult = *data.m_evmcResult;

        // On EVM failure, rollback EVM state changes (op-geth behavior)
        if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
        {
            co_await data.m_rollbackableStorage.rollback(data.m_afterBuyGasSavepoint);
        }

        // Standard refund: unused gas (op-geth refundGas)
        const int64_t refundGasUnits =
            std::max<int64_t>(0, data.m_gasLimit - data.m_gasUsed);
        if (refundGasUnits > 0)
        {
            auto& msg = data.m_hostContext.message();
            auto senderAccount = getAccount(data.m_hostContext, msg.sender);
            auto refund = u256(refundGasUnits) * gasPrice;
            auto balance = co_await senderAccount.balance();
            co_await senderAccount.setBalance(balance + refund);
        }

        // OP-Stack: L1 fee to L1FeeRecipient (op-geth:720-725)
        if (m_l1CostFunc && data.m_rollupCostData)
        {
            uint64_t blockTime = data.m_blockHeader.get().timestamp();
            auto l1Cost = m_l1CostFunc(*data.m_rollupCostData, blockTime);
            if (l1Cost > 0)
            {
                auto l1Recipient = getAccount(data.m_hostContext, m_l1FeeRecipient);
                auto balance = co_await l1Recipient.balance();
                co_await l1Recipient.setBalance(balance + l1Cost);
            }
        }

        // OP-Stack: Operator fee to OperatorFeeRecipient (op-geth:731-732, Isthmus only)
        if (m_isIsthmus && m_operatorCostFunc)
        {
            uint64_t blockTime = data.m_blockHeader.get().timestamp();
            // Operator cost is based on actual gasUsed, not gasLimit (op-geth:731)
            auto operatorFee = m_operatorCostFunc(data.m_gasUsed, blockTime);
            if (operatorFee > 0)
            {
                auto opRecipient = getAccount(data.m_hostContext, m_operatorFeeRecipient);
                auto balance = co_await opRecipient.balance();
                co_await opRecipient.setBalance(balance + operatorFee);
            }
        }
    }

    // ── makeReceipt ──────────────────────────────────────────────────────
    // Standard V2 receipt. OP-Stack adds L1Fee/L1GasUsed fields to receipt.

    template <class Data>
    task::Task<protocol::TransactionReceipt::Ptr> makeReceipt(Data& data)
    {
        auto& evmcResult = *data.m_evmcResult;
        auto& msg = data.m_hostContext.message();

        std::string newContractAddress;
        if (msg.kind == EVMC_CREATE && evmcResult.status_code == EVMC_SUCCESS)
        {
            newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
            boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                std::back_inserter(newContractAddress));
        }

        if (evmcResult.status_code != EVMC_SUCCESS)
        {
            auto [outputData, outputSize, release] = fillErrorOutputInPlace(
                *data.m_executor.get().m_hashImpl, evmcResult.status_code);
            if (release != nullptr)
            {
                if (evmcResult.release != nullptr)
                    evmcResult.release(std::addressof(evmcResult));
                evmcResult.output_data = outputData;
                evmcResult.output_size = outputSize;
                evmcResult.release = release;
            }
        }

        co_return data.m_executor.get().m_receiptFactory.get().createReceipt2(
            data.m_gasUsed, std::move(newContractAddress), data.m_hostContext.logs(),
            static_cast<int32_t>(evmcResult.status),
            {evmcResult.output_data, evmcResult.output_size},
            data.m_blockHeader.get().number(), std::move(data.m_gasPriceStr),
            bcos::protocol::TransactionVersion::V2_VERSION);
    }

private:
    // Deposit transactions: no gas payment, no refund (op-geth preCheck:346-361)
    static bool isDepositTx(const protocol::Transaction& tx)
    {
        // TODO: detect OP deposit transactions from transaction type or attributes.
        // op-geth uses msg.IsDepositTx flag set during tx decoding.
        return false;
    }
};

}  // namespace bcos::executor_v1
