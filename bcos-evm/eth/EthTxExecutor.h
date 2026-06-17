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

namespace bcos::evm
{
#define ETH_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("ETH_TX_EXECUTOR")

struct EthTxExecutor
{
    using PolicyType = bcos::evm_standard::EthPolicy;
    // Standard geth-style gas payment: no legacy consumeBalance, no L1Cost.
    // Only the geth path (buyGas -> execute -> refundGas) without FISCO-specific paths.

    template <class Data>
    task::Task<bool> buyGas(Data& data)
    {
        if (data.m_call) co_return true;

        const auto gasPrice = protocol::effectiveGasPrice(data.m_transaction.get());
        if (gasPrice == 0) co_return true;
        if (data.m_gasLimit <= 0) co_return true;

        const auto maxGasCost = u256(data.m_gasLimit) * gasPrice;
        const auto txValue = u256(data.m_transaction.get().value());
        const auto totalRequired = maxGasCost + txValue;

        auto& msg = data.m_hostContext.message();
        auto senderAccount =
            getAccount(data.m_hostContext, msg.sender);
        auto senderBalance = co_await senderAccount.balance();

        if (senderBalance < totalRequired)
        {
            ETH_TX_EXECUTOR_LOG(ERROR)
                << "buyGas: insufficient balance" << LOG_KV("balance", senderBalance)
                << LOG_KV("maxGasCost", maxGasCost) << LOG_KV("txValue", txValue)
                << LOG_KV("totalRequired", totalRequired);

            // Charge minimum penalty = intrinsic_gas * gasPrice, capped at balance.
            constexpr static int64_t INTRINSIC_GAS = 21000;
            const auto intrinsicCost = u256(INTRINSIC_GAS) * gasPrice;
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
            data.m_gasUsed = (penalty / gasPrice).template convert_to<int64_t>();
            data.m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);
            co_return false;
        }

        co_await senderAccount.setBalance(senderBalance - maxGasCost);
        data.m_afterBuyGasSavepoint = data.m_rollbackableStorage.current();
        data.m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);
        co_return true;
    }

    template <class Data>
    task::Task<void> refundGas(Data& data)
    {
        if (data.m_call) co_return;

        const auto gasPrice = protocol::effectiveGasPrice(data.m_transaction.get());
        if (gasPrice == 0) co_return;

        auto& evmcResult = *data.m_evmcResult;

        // On EVM failure, rollback EVM state changes but keep the pre-deducted gas.
        if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
        {
            co_await data.m_rollbackableStorage.rollback(data.m_afterBuyGasSavepoint);
        }

        const int64_t refundGasUnits =
            std::max<int64_t>(0, data.m_gasLimit - data.m_gasUsed);
        if (refundGasUnits > 0)
        {
            auto& msg = data.m_hostContext.message();
            auto senderAccount =
                getAccount(data.m_hostContext, msg.sender);
            auto refund = u256(refundGasUnits) * gasPrice;
            auto balance = co_await senderAccount.balance();
            co_await senderAccount.setBalance(balance + refund);
        }
    }

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
};

}  // namespace bcos::evm
