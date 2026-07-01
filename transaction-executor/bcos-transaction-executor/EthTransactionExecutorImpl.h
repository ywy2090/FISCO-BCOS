#pragma once

#include "EthTxInputBuilder.h"
#include "RollbackableStorage.h"
#include "bcos-evm/bcos/FiscoStateView.h"
#include "bcos-evm/bcos/StateDiffApplier.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/apply/EthTxFeeSettlement.h"
#include "bcos-evm/eth/eip/TxIntrinsicGas.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/kernel/execution/WarmTransactionEntry.h"
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
template <class TxExec = EthTxFeeSettlement>
class EthTransactionExecutorImpl
{
public:
    explicit EthTransactionExecutorImpl(
        protocol::TransactionReceiptFactory const& receiptFactory, crypto::Hash::Ptr hashImpl)
      : m_receiptFactory(receiptFactory), m_hashImpl(std::move(hashImpl))
    {}

    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    TxExec m_txExecutor;

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
            Rollbackable<Storage>::Savepoint m_afterBuyGasSavepoint{0};
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
            bcos::evm_standard::EthChainPolicy m_policy;
            evmc_message m_message{};
            bcos::evm_standard::RevisionConfig m_revisionConfig{};
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

                state::FiscoStateView stateView(
                    m_data->m_rollbackableStorage, false, *m_data->m_executor.get().m_hashImpl);
                state::State state(stateView);

                state::Transaction tx;
                auto const& msg = m_data->m_message;
                tx.from = msg.sender;
                if (msg.kind != EVMC_CREATE && msg.kind != EVMC_CREATE2)
                {
                    tx.to = msg.recipient;
                }
                tx.data.assign(msg.input_data, msg.input_data + msg.input_size);
                tx.value = fromEvmC(msg.value);
                tx.gasPrice = m_data->m_gasPriceLegacy;
                tx.gasLimit = msg.gas;

                state::TransactionProperties props;
                props.warmDestination = !isCreateKind(msg.kind);
                execution::warmTransactionEntry(state, m_data->m_revisionConfig, nullptr, tx,
                    m_data->m_blockInfo, props, m_data->m_web3AccessListResolved.accessList.get(),
                    m_data->m_web3TypedTxKind);
            }
            else if constexpr (phase == static_cast<int>(EthExecutePhase::Execute))
            {
                if (!m_data->m_gasFieldsFilled)
                {
                    eth_tx::fillTransactionGasFields(m_data->m_transaction.get(), *m_data);
                }

                if (!m_data->m_call)
                {
                    if (!co_await m_data->m_executor.get().m_txExecutor.buyGas(*m_data))
                    {
                        co_return {};
                    }
                }

                auto output = co_await applyEthMessageTx();
                m_data->m_message = std::move(output.message);
                m_data->m_revisionConfig = std::move(output.revisionConfig);
                m_data->m_receiptLogs = std::move(output.receiptLogs);
                m_data->m_gasSettlementSnapshot = output.gasSettlementSnapshot;
                m_data->m_evmcResult.emplace(std::move(output.evmcResult));

                if (m_data->m_evmcResult->status_code == EVMC_SUCCESS ||
                    m_data->m_evmcResult->status_code == EVMC_REVERT)
                {
                    if (!output.stateDiff.accounts.empty())
                    {
                        co_await state::applyStateDiff(m_data->m_rollbackableStorage,
                            output.stateDiff, false, *m_data->m_executor.get().m_hashImpl,
                            m_data->m_transaction.get().abi());
                    }
                }

                settleGasUsedFromEvmResult();

                if (!m_data->m_call)
                {
                    co_await m_data->m_executor.get().m_txExecutor.refundGas(*m_data);
                }
            }
            else if constexpr (phase == static_cast<int>(EthExecutePhase::Finalize))
            {
                co_return co_await m_data->m_executor.get().m_txExecutor.makeReceipt(*m_data);
            }
            co_return {};
        }

        void settleGasUsedFromEvmResult()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            auto const& snapshot = m_data->m_gasSettlementSnapshot;
            auto const isWeb3 =
                m_data->m_transaction.get().type() == protocol::TransactionType::Web3Transaction;
            auto const eip7623 = m_data->m_revisionConfig.eip7623;

            if (snapshot.gasLimit > 0 && isWeb3 && eip7623)
            {
                m_data->m_gasUsed = gas::settleTopLevelTransactionGas(m_data->m_gasLimit,
                    evmcResult.gas_left, snapshot.evmGasRefund,
                    m_data->m_revisionConfig.calldata_floor_per_token, snapshot.calldata);
                return;
            }

            m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
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
            input.txNonce = m_data->m_nonce.convert_to<uint64_t>();
            input.txHash = m_data->m_transaction.get().hash();

            auto output = co_await applyEthMessage(std::move(input));
            m_data->m_topLevelIncludedTxVmError = output.topLevelIncludedTxVmError;
            co_return output;
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
using EthTransactionExecutorImpl = bcos::evm::EthTransactionExecutorImpl<>;
}  // namespace bcos::executor_v1
