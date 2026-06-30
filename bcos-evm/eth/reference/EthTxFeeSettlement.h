#pragma once
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/pipeline/FeeInputsMapping.h"
#include "bcos-evm/eth/policy/EthChainPolicy.h"
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>

namespace bcos::evm
{
#define ETH_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("ETH_TX_EXECUTOR")

struct EthTxFeeSettlement
{
    using PolicyType = bcos::evm_standard::EthChainPolicy;
    // Standard geth-style gas payment: no legacy consumeBalance, no L1Cost.
    // Only the geth path (buyGas -> execute -> refundGas) without FISCO-specific paths.

    template <class Data>
    task::Task<bool> buyGas(Data& data)
    {
        if (data.m_call)
            co_return true;

        auto const feeInputs =
            gas::toFeeInputs(data.m_executionContext.revisionConfig, data.m_blockInfo,
                gas::FeeCapsView{data.m_gasPriceLegacy, data.m_gasTipCap, data.m_gasFeeCap,
                    data.m_web3TypedTxKind, data.m_hasExplicitFeeCaps},
                data.m_gasLimit);
        auto const plan = gas::planPreExecution(feeInputs);
        data.m_effectiveGasPrice = plan.effectiveGasPrice;
        if (data.m_effectiveGasPrice == 0)
            co_return true;
        if (data.m_gasLimit <= 0)
            co_return true;

        const auto maxGasCost = plan.preDebitAmount;
        const auto txValue = u256(data.m_transaction.get().value());
        const auto totalRequired = plan.maxBalanceDebit + txValue;

        auto& msg = data.m_executionContext.message;
        ledger::account::EVMAccount senderAccount(data.m_rollbackableStorage, msg.sender, false);
        auto senderBalance = co_await senderAccount.balance();

        if (senderBalance < totalRequired)
        {
            ETH_TX_EXECUTOR_LOG(ERROR)
                << "buyGas: insufficient balance" << LOG_KV("balance", senderBalance)
                << LOG_KV("maxGasCost", maxGasCost) << LOG_KV("txValue", txValue)
                << LOG_KV("totalRequired", totalRequired);

            // Charge minimum penalty = intrinsic_gas * gasPrice, capped at balance.
            const auto intrinsicCost = u256(gas::TX_BASE_GAS) * data.m_effectiveGasPrice;
            const auto penalty = std::min(senderBalance, intrinsicCost);
            if (penalty > 0)
            {
                co_await senderAccount.setBalance(senderBalance - penalty);
            }

            evmc_result failResult{};
            failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
            failResult.gas_left = 0;
            failResult.output_data = nullptr;
            failResult.output_size = 0;
            failResult.release = nullptr;
            failResult.create_address = {};
            data.m_evmcResult.emplace(
                EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash));
            data.m_gasUsed = (penalty / data.m_effectiveGasPrice).template convert_to<int64_t>();
            data.m_gasPriceStr = "0x" + data.m_effectiveGasPrice.str(256, std::ios_base::hex);
            co_return false;
        }

        co_await senderAccount.setBalance(senderBalance - maxGasCost);
        data.m_afterBuyGasSavepoint = data.m_rollbackableStorage.current();
        data.m_gasPriceStr = "0x" + data.m_effectiveGasPrice.str(256, std::ios_base::hex);
        co_return true;
    }

    template <class Data>
    task::Task<void> refundGas(Data& data)
    {
        if (data.m_call)
            co_return;

        if (data.m_effectiveGasPrice == 0)
            co_return;

        auto& evmcResult = *data.m_evmcResult;

        // On EVM failure, rollback EVM state changes but keep the pre-deducted gas.
        if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
        {
            co_await data.m_rollbackableStorage.rollback(data.m_afterBuyGasSavepoint);
        }

        auto const feeInputs =
            gas::toFeeInputs(data.m_executionContext.revisionConfig, data.m_blockInfo,
                gas::FeeCapsView{data.m_gasPriceLegacy, data.m_gasTipCap, data.m_gasFeeCap,
                    data.m_web3TypedTxKind, data.m_hasExplicitFeeCaps},
                data.m_gasLimit);
        const int64_t refundGasUnits = std::max<int64_t>(0, data.m_gasLimit - data.m_gasUsed);
        auto const plan = gas::planPostExecution(feeInputs, data.m_gasUsed, refundGasUnits);

        if (plan.unusedRefund > 0)
        {
            auto& msg = data.m_executionContext.message;
            ledger::account::EVMAccount senderAccount(
                data.m_rollbackableStorage, msg.sender, false);
            auto balance = co_await senderAccount.balance();
            co_await senderAccount.setBalance(balance + plan.unusedRefund);
        }

        if (plan.coinbaseTip > 0)
        {
            ledger::account::EVMAccount coinbaseAccount(
                data.m_rollbackableStorage, data.m_blockInfo.coinbase, false);
            if (!co_await coinbaseAccount.exists())
            {
                co_await coinbaseAccount.create();
            }
            auto balance = co_await coinbaseAccount.balance();
            co_await coinbaseAccount.setBalance(balance + plan.coinbaseTip);
        }
    }

    template <class Data>
    task::Task<protocol::TransactionReceipt::Ptr> makeReceipt(Data& data)
    {
        auto& evmcResult = *data.m_evmcResult;
        auto& msg = data.m_executionContext.message;

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
            auto [outputData, outputSize, release] =
                fillErrorOutputInPlace(*data.m_executor.get().m_hashImpl, evmcResult.status_code);
            if (release != nullptr)
            {
                if (evmcResult.release != nullptr)
                    evmcResult.release(std::addressof(evmcResult));
                evmcResult.output_data = outputData;
                evmcResult.output_size = outputSize;
                evmcResult.release = release;
            }
        }

        co_return data.m_executor.get().m_receiptFactory.get().createReceipt2(data.m_gasUsed,
            std::move(newContractAddress), data.m_executionContext.logs,
            static_cast<int32_t>(evmcResult.status),
            {evmcResult.output_data, evmcResult.output_size}, data.m_blockHeader.get().number(),
            std::move(data.m_gasPriceStr), bcos::protocol::TransactionVersion::V2_VERSION);
    }
};

}  // namespace bcos::evm
