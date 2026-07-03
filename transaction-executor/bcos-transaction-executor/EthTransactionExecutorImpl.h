#pragma once

#include "EthTxInputBuilder.h"
#include "RollbackableStorage.h"
#include "bcos-evm/bcos/FiscoStateView.h"
#include "bcos-evm/bcos/StateDiffApplier.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/policy/EthChainPolicy.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <boost/algorithm/hex.hpp>
#include <memory>

namespace bcos::evm
{

enum class EthExecutePhase : uint8_t
{
    Prepare = 0,
    Execute = 1,
    Finalize = 2
};

#define ETH_TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("ETH_TRANSACTION_EXECUTOR")

evmc_message newEVMCMessage(bcos::protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin);

/// Pure-ethereum transaction executor — geth-aligned gas + applyEthMessage, no FISCO
/// extensions.
class EthTransactionExecutorImpl
{
public:
    explicit EthTransactionExecutorImpl(
        protocol::TransactionReceiptFactory const& receiptFactory, crypto::Hash::Ptr hashImpl)
      : m_receiptFactory(receiptFactory), m_hashImpl(std::move(hashImpl))
    {}

    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;

    static int64_t computeEffectiveGasLimit(
        protocol::Transaction const& tx, int64_t blockGasLimit) noexcept
    {
        if (tx.gasLimit() > 0)
        {
            return std::min<int64_t>(tx.gasLimit(), blockGasLimit);
        }
        return blockGasLimit;
    }

    template <class Storage>
    struct ExecuteContext
    {
        struct Data
        {
            std::reference_wrapper<EthTransactionExecutorImpl> m_executor;
            std::reference_wrapper<protocol::BlockHeader const> m_blockHeader;
            std::reference_wrapper<protocol::Transaction const> m_transaction;
            int m_contextID;
            std::reference_wrapper<ledger::LedgerConfig const> m_ledgerConfig;
            Rollbackable<Storage> m_rollbackableStorage;
            Rollbackable<Storage>::Savepoint m_startSavepoint;
            bool m_call;
            int64_t m_gasUsed{0};
            std::string m_gasPriceStr;
            int64_t m_gasLimit;
            evmc_address m_origin{};
            u256 m_nonce;
            executor::Web3AccessListResolved m_web3AccessListResolved;
            bcos::u256 m_gasTipCap{0}, m_gasFeeCap{0}, m_gasPriceLegacy{0}, m_effectiveGasPrice{0};
            bool m_hasExplicitFeeCaps{false};
            uint8_t m_web3TypedTxKind{0};
            state::BlockInfo m_blockInfo{};
            bool m_topLevelIncludedTxVmError{false};
            bool m_gasFieldsFilled{false};
            evmc::VM m_vm;
            bcos::evm::EthChainPolicy m_policy;
            evmc_message m_message{};
            bcos::evm::RevisionConfig m_revisionConfig{};
            std::vector<protocol::LogEntry> m_receiptLogs;
            gas::TxGasSettlementContext m_gasSettlementSnapshot{};
            std::optional<EVMCResult> m_evmcResult;

            Data(EthTransactionExecutorImpl& executor, Storage& storage,
                protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
                int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
              : m_executor(executor),
                m_blockHeader(blockHeader),
                m_transaction(transaction),
                m_contextID(contextID),
                m_ledgerConfig(ledgerConfig),
                m_rollbackableStorage(storage),
                m_startSavepoint(m_rollbackableStorage.current()),
                m_call(call),
                m_gasLimit(computeEffectiveGasLimit(
                    transaction, static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit())))),
                m_origin((!transaction.sender().empty() &&
                             transaction.sender().size() == sizeof(evmc_address)) ?
                             *reinterpret_cast<evmc_address const*>(transaction.sender().data()) :
                             evmc_address{}),
                m_nonce(hex2u(transaction.nonce())),
                m_web3AccessListResolved(executor::resolveWeb3AccessList(transaction)),
                m_vm(evmc_create_evmone())
            {
                m_revisionConfig = m_policy.computeRevisionConfig(blockHeader);
            }
        };
        std::unique_ptr<Data> m_data;

        ExecuteContext(EthTransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
          : m_data(std::make_unique<Data>(
                executor, storage, blockHeader, transaction, contextID, ledgerConfig, call))
        {}

        template <int phase>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            if constexpr (phase == static_cast<int>(EthExecutePhase::Prepare))
            {
                eth_tx::fillTransactionGasFields(m_data->m_transaction.get(), *m_data);
                m_data->m_gasFieldsFilled = true;
            }
            else if constexpr (phase == static_cast<int>(EthExecutePhase::Execute))
            {
                if (!m_data->m_gasFieldsFilled)
                {
                    eth_tx::fillTransactionGasFields(m_data->m_transaction.get(), *m_data);
                }

                auto output = co_await applyEthMessageTx();
                m_data->m_message = std::move(output.message);
                m_data->m_revisionConfig = std::move(output.revisionConfig);
                m_data->m_receiptLogs = std::move(output.receiptLogs);
                m_data->m_gasSettlementSnapshot = output.gasSettlementSnapshot;
                m_data->m_evmcResult.emplace(std::move(output.evmcResult));
                m_data->m_gasUsed = output.gasUsed;
                m_data->m_effectiveGasPrice = output.effectiveGasPrice;
                m_data->m_gasPriceStr = std::move(output.gasPriceStr);
                m_data->m_topLevelIncludedTxVmError = output.topLevelIncludedTxVmError;

                if (!m_data->m_call && !output.stateDiff.accounts.empty())
                {
                    co_await state::applyStateDiff(m_data->m_rollbackableStorage, output.stateDiff,
                        false, *m_data->m_executor.get().m_hashImpl,
                        m_data->m_transaction.get().abi());
                }
            }
            else if constexpr (phase == static_cast<int>(EthExecutePhase::Finalize))
            {
                co_return co_await makeReceiptFromData();
            }
            co_return {};
        }

        task::Task<EthMessageResult> applyEthMessageTx()
        {
            state::FiscoStateView stateView(
                m_data->m_rollbackableStorage, false, *m_data->m_executor.get().m_hashImpl);

            EthMessageRequest input;
            input.stateView = std::addressof(stateView);
            input.vm = std::addressof(m_data->m_vm);
            input.hashImpl = m_data->m_executor.get().m_hashImpl.get();
            input.message = m_data->m_message;
            input.blockHashes = state::buildFiscoBlockHashes(
                m_data->m_rollbackableStorage, m_data->m_blockHeader.get().number());
            input.revisionConfig = m_data->m_revisionConfig;
            input.gasPrice = m_data->m_gasPriceLegacy;
            input.gasTipCap = m_data->m_gasTipCap;
            input.gasFeeCap = m_data->m_gasFeeCap;
            input.hasExplicitFeeCaps = m_data->m_hasExplicitFeeCaps;
            input.blockInfo = m_data->m_blockInfo;
            eth_tx::fillWeb3Fields(m_data->m_transaction.get(), input);
            if (input.web3TypedTxKind == 0)
            {
                input.web3TypedTxKind = m_data->m_web3TypedTxKind;
            }
            input.hasExplicitFeeCaps = m_data->m_hasExplicitFeeCaps;
            input.txNonce = m_data->m_nonce.template convert_to<uint64_t>();
            input.txHash = m_data->m_transaction.get().hash();
            input.isCall = m_data->m_call;
            input.txValue = u256(m_data->m_transaction.get().value());

            co_return co_await applyEthMessage(std::move(input));
        }

        task::Task<protocol::TransactionReceipt::Ptr> makeReceiptFromData()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            auto& msg = m_data->m_message;

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
                    *m_data->m_executor.get().m_hashImpl, evmcResult.status_code);
                if (release != nullptr)
                {
                    if (evmcResult.release != nullptr)
                        evmcResult.release(std::addressof(evmcResult));
                    evmcResult.output_data = outputData;
                    evmcResult.output_size = outputSize;
                    evmcResult.release = release;
                }
            }

            co_return m_data->m_executor.get().m_receiptFactory.get().createReceipt2(
                m_data->m_gasUsed, std::move(newContractAddress), m_data->m_receiptLogs,
                static_cast<int32_t>(evmcResult.status),
                {evmcResult.output_data, evmcResult.output_size},
                m_data->m_blockHeader.get().number(), std::move(m_data->m_gasPriceStr),
                bcos::protocol::TransactionVersion::V2_VERSION);
        }

    private:
        static bool isCreateKind(evmc_call_kind kind) noexcept
        {
            return kind == EVMC_CREATE || kind == EVMC_CREATE2;
        }
    };

    template <class Storage>
    auto createExecuteContext(Storage& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call) -> task::Task<ExecuteContext<Storage>>
    {
        ETH_TRANSACTION_EXECUTOR_LOG(TRACE) << "Create eth transaction context";
        auto ctx = ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
        ctx.m_data->m_message = newEVMCMessage(
            blockHeader.number(), transaction, ctx.m_data->m_gasLimit, ctx.m_data->m_origin);
        co_return ctx;
    }

    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        auto ctx = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call);
        co_await ctx.template executeStep<0>();
        co_await ctx.template executeStep<1>();
        co_return co_await ctx.template executeStep<2>();
    }
};

}  // namespace bcos::evm

namespace bcos::executor_v1
{
using EthTransactionExecutorImpl = bcos::evm::EthTransactionExecutorImpl;
}  // namespace bcos::executor_v1
