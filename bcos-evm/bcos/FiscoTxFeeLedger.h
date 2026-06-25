#pragma once
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>

namespace bcos::evm
{

DERIVE_BCOS_EXCEPTION(InvalidReceiptVersion);

#define FISCO_TX_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("FISCO_TX_EXECUTOR")

struct FiscoTxFeeLedger
{
    using PolicyType = bcos::chain_policy::FiscoPolicy;
    // FIB-75 (geth-style): Pre-deduct gasLimit * gasPrice from sender before EVM execution.
    // If balance is insufficient to cover gas + value, fail immediately (EVM does not run,
    // no balance deducted, nonce preserved as replay protection).
    // On success, balance -= gasLimit * gasPrice; EVM then runs with m_gasLimit as its
    // gas budget, guaranteeing gasUsed <= gasLimit so refundGas() always has enough to
    // settle without confiscating extra balance.
    template <class Data>
    task::Task<bool> buyGas(Data& data)
    {
        if (data.m_call)
        {
            co_return true;
        }

        // FIB-75: use the transaction's effective gas price (legacy gasPrice or EIP-1559
        // maxFeePerGas) so charging matches what the txpool validated.
        const auto gasPrice = protocol::effectiveGasPrice(data.m_transaction.get());
        if (gasPrice == 0)
        {
            co_return true;
        }

        if (data.m_gasLimit <= 0)
        {
            co_return true;
        }

        const auto maxGasCost = u256(data.m_gasLimit) * gasPrice;
        const auto txValue = u256(data.m_transaction.get().value());
        const auto totalRequired = maxGasCost + txValue;

        auto& evmcMessage = data.m_executionContext.message;
        ledger::account::EVMAccount senderAccount(data.m_rollbackableStorage, evmcMessage.sender,
            data.m_executionContext.revisionConfig.use_raw_address);
        auto senderBalance = co_await senderAccount.balance();

        if (senderBalance < totalRequired)
        {
            FISCO_TX_EXECUTOR_LOG(ERROR)
                << "buyGas: insufficient balance" << LOG_KV("balance", senderBalance)
                << LOG_KV("maxGasCost", maxGasCost) << LOG_KV("txValue", txValue)
                << LOG_KV("totalRequired", totalRequired);

            // FIB-75: charge minimum penalty = min(balance, intrinsic_gas * gasPrice).
            // The transaction is already in a consensus-packed block and consumed
            // consensus/storage resources, so a sender who can't cover full gas cost
            // still pays at least the base tx gas cost (geth's intrinsic gas for
            // an empty tx). If balance < intrinsic cost, drain what's left. This
            // prevents free spam from repeatedly submitting under-funded transactions.
            const auto intrinsicCost = u256(gas::TX_BASE_GAS) * gasPrice;
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
            // gasUsed reflects what was actually charged as penalty (in gas units).
            data.m_gasUsed = (penalty / gasPrice).template convert_to<int64_t>();
            data.m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);

            co_return false;
        }

        // Pre-deduct max gas cost from sender
        co_await senderAccount.setBalance(senderBalance - maxGasCost);
        data.m_afterBuyGasSavepoint = data.m_rollbackableStorage.current();
        data.m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);
        co_return true;
    }

    // FIB-75 (geth-style): After EVM execution, refund (gasLimit - gasUsed) * gasPrice.
    // If EVM failed (non-SUCCESS, non-REVERT), roll back state changes while preserving
    // the pre-deducted gas cost. gasUsed <= gasLimit is guaranteed because the EVM's
    // gas budget is m_gasLimit.
    template <class Data>
    task::Task<void> refundGas(Data& data)
    {
        auto& evmcResult = *data.m_evmcResult;

        if (data.m_call)
        {
            co_return;
        }

        // FIB-75: mirror buyGas() — use the tx's effective gas price.
        const auto gasPrice = protocol::effectiveGasPrice(data.m_transaction.get());
        if (gasPrice == 0)
        {
            co_return;
        }

        // On EVM failure (not SUCCESS / REVERT), rollback EVM state changes but keep
        // the pre-deducted gas — the sender still pays for the wasted execution.
        if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
        {
            co_await data.m_rollbackableStorage.rollback(data.m_afterBuyGasSavepoint);
        }

        // Refund unused gas (settled gasUsed when EIP-7623 path active)
        const int64_t refundGasUnits = std::max<int64_t>(0, data.m_gasLimit - data.m_gasUsed);
        if (refundGasUnits > 0)
        {
            auto refund = u256(refundGasUnits) * gasPrice;
            auto& evmcMessage = data.m_executionContext.message;
            ledger::account::EVMAccount senderAccount(data.m_rollbackableStorage,
                evmcMessage.sender, data.m_executionContext.revisionConfig.use_raw_address);
            auto balance = co_await senderAccount.balance();
            co_await senderAccount.setBalance(balance + refund);
        }
    }

    // Legacy balance consumption — only used when bugfix_gas_payment_balance_precheck is OFF.
    // Kept unchanged from pre-FIB-75 behavior: on insufficient balance, rollback execution
    // effects and deduct nothing (the original FIB-75 bug that's fixed by the precheck flag).
    template <class Data>
    task::Task<void> consumeBalance(Data& data)
    {
        auto& evmcResult = *data.m_evmcResult;
        if (!data.m_call)
        {
            auto& evmcMessage = data.m_executionContext.message;
            if (auto gasPrice = u256{std::get<0>(data.m_ledgerConfig.get().gasPrice())};
                gasPrice > 0)
            {
                constexpr static const auto GAS_PRICE_DIGITS = 256;
                data.m_gasPriceStr = "0x" + gasPrice.str(GAS_PRICE_DIGITS, std::ios_base::hex);

                auto balanceUsed = data.m_gasUsed * gasPrice;
                ledger::account::EVMAccount senderAccount(data.m_rollbackableStorage,
                    evmcMessage.sender, data.m_executionContext.revisionConfig.use_raw_address);
                auto senderBalance = co_await senderAccount.balance();

                if (senderBalance < balanceUsed)
                {
                    FISCO_TX_EXECUTOR_LOG(ERROR) << "Insufficient balance: " << senderBalance
                                                 << ", balanceUsed: " << balanceUsed;
                    evmcResult.status_code = EVMC_INSUFFICIENT_BALANCE;
                    evmcResult.status = protocol::TransactionStatus::NotEnoughCash;
                    if (evmcResult.release != nullptr)
                    {
                        evmcResult.release(std::addressof(evmcResult));
                    }
                    evmcResult.output_data = nullptr;
                    evmcResult.output_size = 0;
                    evmcResult.release = nullptr;
                    evmcResult.create_address = {};
                    co_await data.m_rollbackableStorage.rollback(data.m_startSavepoint);
                }
                else
                {
                    co_await senderAccount.setBalance(senderBalance - balanceUsed);
                }
            }
        }
    }

    template <class Data>
    task::Task<protocol::TransactionReceipt::Ptr> makeReceipt(Data& data)
    {
        const auto& evmcMessage = data.m_executionContext.message;
        auto& evmcResult = *data.m_evmcResult;

        std::string newContractAddress;
        if (evmcMessage.kind == EVMC_CREATE && evmcResult.status_code == EVMC_SUCCESS)
        {
            newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
            boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                std::back_inserter(newContractAddress));
        }

        if (evmcResult.status_code != EVMC_SUCCESS)
        {
            FISCO_TX_EXECUTOR_LOG(DEBUG) << "Transaction revert: " << evmcResult.status_code;

            auto [outputData, outputSize, release] =
                fillErrorOutputInPlace(*data.m_executor.get().m_hashImpl, evmcResult.status_code);
            if (release != nullptr)
            {
                if (evmcResult.release != nullptr)
                {
                    evmcResult.release(std::addressof(evmcResult));
                }
                evmcResult.output_data = outputData;
                evmcResult.output_size = outputSize;
                evmcResult.release = release;
            }
            if (data.m_executionContext.revisionConfig.fix_revert_logs)
            {
                data.m_executionContext.logs.clear();
            }
        }

        auto receiptStatus = static_cast<int32_t>(evmcResult.status);
        auto const& logEntries = data.m_executionContext.logs;
        protocol::TransactionReceipt::Ptr receipt;
        switch (auto transactionVersion = static_cast<bcos::protocol::TransactionVersion>(
                    data.m_transaction.get().version()))
        {
        case bcos::protocol::TransactionVersion::V0_VERSION:
            receipt = data.m_executor.get().m_receiptFactory.get().createReceipt(data.m_gasUsed,
                std::move(newContractAddress), logEntries, receiptStatus,
                {evmcResult.output_data, evmcResult.output_size},
                data.m_blockHeader.get().number());
            break;
        case bcos::protocol::TransactionVersion::V1_VERSION:
        case bcos::protocol::TransactionVersion::V2_VERSION:
            receipt = data.m_executor.get().m_receiptFactory.get().createReceipt2(data.m_gasUsed,
                std::move(newContractAddress), logEntries, receiptStatus,
                {evmcResult.output_data, evmcResult.output_size}, data.m_blockHeader.get().number(),
                std::move(data.m_gasPriceStr), transactionVersion);
            break;
        default:
            BOOST_THROW_EXCEPTION(InvalidReceiptVersion{} << bcos::errinfo_comment(
                                      "Invalid receipt version: " +
                                      std::to_string(data.m_transaction.get().version())));
        }

        FISCO_TX_EXECUTOR_LOG(TRACE) << "Execute transaction finished: " << *receipt;
        co_return receipt;  // Complete the third step
    }
};

}  // namespace bcos::evm
