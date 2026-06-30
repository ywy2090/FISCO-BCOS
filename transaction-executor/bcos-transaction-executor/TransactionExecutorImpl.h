#pragma once

#include "RollbackableStorage.h"
#include "adapters/AuthCheck.h"
#include "adapters/ExecutorAuthAdapter.h"
#include "adapters/ExecutorPrecompileAdapter.h"
#include "adapters/FiscoPortAdapterContext.h"
#include "adapters/PrecompiledImpl.h"
#include "adapters/PrecompiledManager.h"
#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "bcos-evm/bcos/FiscoBlockInfo.h"
#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-evm/bcos/FiscoPrepareTransaction.h"
#include "bcos-evm/bcos/FiscoStateView.h"
#include "bcos-evm/bcos/FiscoTxFeeSettlement.h"
#include "bcos-evm/bcos/StateDiffApplier.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/eip/TxIntrinsicGas.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <evmc/evmc.hpp>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>

namespace bcos::evm
{

enum class ExecutePhase : uint8_t
{
    Prepare = 0,
    Execute = 1,
    Finalize = 2
};

#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

evmc_message newEVMCMessage(bcos::protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin);

template <class TxExec = FiscoTxFeeSettlement>
class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl, PrecompiledManager& precompiledManager)
      : m_receiptFactory(receiptFactory),
        m_hashImpl(std::move(hashImpl)),
        m_precompiledManager(precompiledManager)
    {}

    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    std::reference_wrapper<PrecompiledManager> m_precompiledManager;
    TxExec m_txExecutor;

    // FIB-75: Effective gas limit for EVM execution.
    // When fix_gas_precheck is enabled and the tx declares gasLimit > 0,
    // the EVM budget is capped at tx.gasLimit() (geth-compatible); otherwise falls back to
    // blockGasLimit for backward compat.
    static int64_t computeEffectiveGasLimit(
        protocol::Transaction const& tx, int64_t txSysGasLimit, bool fixGasPrecheck)
    {
        if (fixGasPrecheck && tx.gasLimit() > 0)
        {
            return std::min<int64_t>(tx.gasLimit(), txSysGasLimit);
        }
        return txSysGasLimit;
    }

    template <class Storage>
    struct ExecuteContext
    {
        struct Data
        {
            std::reference_wrapper<TransactionExecutorImpl> m_executor;
            std::reference_wrapper<protocol::BlockHeader const> m_blockHeader;
            std::reference_wrapper<protocol::Transaction const> m_transaction;
            int m_contextID;
            std::reference_wrapper<ledger::LedgerConfig const> m_ledgerConfig;
            Rollbackable<Storage> m_rollbackableStorage;
            Rollbackable<Storage>::Savepoint m_startSavepoint;
            // FIB-75: savepoint right after buyGas() pre-deduction — used to rollback only EVM
            // effects while preserving the pre-deducted balance.
            Rollbackable<Storage>::Savepoint m_afterBuyGasSavepoint{0};
            bool m_call;
            int64_t m_gasUsed = 0;
            std::string m_gasPriceStr;

            bcos::chain_policy::FiscoPolicy m_policy;
            bcos::evm::FiscoExecutionArtifacts m_executionContext;
            int64_t m_gasLimit;
            int64_t m_seq = 0;
            evmc_address m_origin;
            u256 m_nonce;
            executor::Web3AccessListResolved m_web3AccessListResolved;
            evmc::VM m_vm;
            std::optional<EVMCResult> m_evmcResult;

            Data(TransactionExecutorImpl& executor, Storage& storage,
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
                m_policy(ledgerConfig.features(), ledgerConfig.balanceTransfer(),
                    ledgerConfig.authCheckStatus() != 0),
                m_executionContext(),
                m_gasLimit(computeEffectiveGasLimit(transaction,
                    static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit())),
                    [&]() {
                        m_executionContext.revisionConfig =
                            m_policy.computeRevisionConfig(blockHeader);
                        return m_executionContext.revisionConfig.fix_gas_precheck;
                    }())),
                m_origin((!m_transaction.get().sender().empty() &&
                             m_transaction.get().sender().size() == sizeof(evmc_address)) ?
                             *(evmc_address*)m_transaction.get().sender().data() :
                             evmc_address{}),
                m_nonce(hex2u(transaction.nonce())),
                m_web3AccessListResolved(executor::resolveWeb3AccessList(transaction)),
                m_vm(evmc_create_evmone())
            {}
        };
        std::unique_ptr<Data> m_data;

        ExecuteContext(TransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
          : m_data(std::make_unique<Data>(
                executor, storage, blockHeader, transaction, contextID, ledgerConfig, call))
        {}

        template <int phase>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            if constexpr (phase == static_cast<int>(ExecutePhase::Prepare))
            {
                bcos::evm::state::FiscoStateView stateView(m_data->m_rollbackableStorage,
                    m_data->m_executionContext.revisionConfig.use_raw_address,
                    *m_data->m_executor.get().m_hashImpl);
                bcos::evm::state::State state(stateView);
                auto const blockInfo = bcos::evm::state::buildFiscoBlockInfo(
                    m_data->m_blockHeader.get(), m_data->m_ledgerConfig.get(),
                    [policy = m_data->m_policy](
                        int64_t timestamp) { return policy.convertTimestamp(timestamp); });

                state::Transaction tx;
                auto const& msg = m_data->m_executionContext.message;
                tx.from = msg.sender;
                if (msg.kind != EVMC_CREATE && msg.kind != EVMC_CREATE2)
                {
                    tx.to = msg.recipient;
                }
                tx.data.assign(msg.input_data, msg.input_data + msg.input_size);
                tx.value = fromEvmC(msg.value);
                tx.gasPrice = protocol::effectiveGasPrice(m_data->m_transaction.get());
                tx.gasLimit = msg.gas;
                prepareTransaction(state, tx, blockInfo,
                    FiscoPrepareTransactionInput{
                        .revisionConfig = m_data->m_executionContext.revisionConfig.eth(),
                        .properties = {},
                        .accessList = m_data->m_web3AccessListResolved.accessList.get()});
            }
            else if constexpr (phase == static_cast<int>(ExecutePhase::Execute))
            {
                auto updated = co_await updateNonce();
                if (updated)
                    m_data->m_startSavepoint = m_data->m_rollbackableStorage.current();

                if (!m_data->m_call && m_data->m_executionContext.revisionConfig.fix_gas_precheck)
                {
                    if (!co_await m_data->m_executor.get().m_txExecutor.buyGas(*m_data))
                        co_return {};
                }
                auto output = co_await applyFiscoMessageTx();
                m_data->m_executionContext = std::move(output.executionContext);
                m_data->m_evmcResult.emplace(std::move(output.evmcResult));
                if (m_data->m_evmcResult->status_code == EVMC_SUCCESS)
                {
                    co_await bcos::evm::state::applyStateDiff(m_data->m_rollbackableStorage,
                        output.stateDiff, m_data->m_executionContext.revisionConfig.use_raw_address,
                        *m_data->m_executor.get().m_hashImpl, m_data->m_transaction.get().abi());
                }
                settleGasUsedFromEvmResult();
                if (!m_data->m_call)
                {
                    if (m_data->m_executionContext.revisionConfig.fix_gas_precheck)
                    {
                        co_await m_data->m_executor.get().m_txExecutor.refundGas(*m_data);
                    }
                    else
                    {
                        co_await m_data->m_executor.get().m_txExecutor.consumeBalance(*m_data);
                    }
                }
            }
            else if constexpr (phase == static_cast<int>(ExecutePhase::Finalize))
            {
                co_return co_await m_data->m_executor.get().m_txExecutor.makeReceipt(*m_data);
            }

            co_return {};
        }

        task::Task<bool> updateNonce()
        {
            if (const auto& transaction = m_data->m_transaction.get();
                transaction.type() == protocol::TransactionType::Web3Transaction)
            {
                auto& callNonce = m_data->m_nonce;
                ledger::account::EVMAccount account(m_data->m_rollbackableStorage, m_data->m_origin,
                    m_data->m_executionContext.revisionConfig.use_raw_address);

                if (!co_await account.exists())
                {
                    co_await account.create();
                }
                auto nonceInStorage = co_await account.nonce();
                auto storageNonce = u256(nonceInStorage.value_or("0"));
                u256 newNonce = std::max(callNonce, storageNonce) + 1;
                co_await account.setNonce(newNonce.convert_to<std::string>());
                co_return true;
            }
            co_return false;
        }

        void settleGasUsedFromEvmResult()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            auto const& snapshot = m_data->m_executionContext.gasSettlementSnapshot;
            if (snapshot.gasLimit > 0 &&
                m_data->m_transaction.get().type() == protocol::TransactionType::Web3Transaction &&
                m_data->m_executionContext.revisionConfig.eth().eip7623)
            {
                m_data->m_gasUsed = gas::settleTopLevelTransactionGas(m_data->m_gasLimit,
                    evmcResult.gas_left, snapshot.evmGasRefund,
                    m_data->m_executionContext.revisionConfig.eth().calldata_floor_per_token,
                    snapshot.calldata);
            }
            else
            {
                m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
            }
        }

        task::Task<FiscoExecutionResult> applyFiscoMessageTx()
        {
            FiscoExecutionRequest input;
            input.vm = std::addressof(m_data->m_vm);
            input.hashImpl = m_data->m_executor.get().m_hashImpl.get();
            input.message = m_data->m_executionContext.message;
            input.blockInfo = bcos::evm::state::buildFiscoBlockInfo(m_data->m_blockHeader.get(),
                m_data->m_ledgerConfig.get(), [policy = m_data->m_policy](int64_t timestamp) {
                    return policy.convertTimestamp(timestamp);
                });
            input.blockHashes = bcos::evm::state::buildFiscoBlockHashes(
                m_data->m_rollbackableStorage, m_data->m_blockHeader.get().number());
            input.revisionConfig = m_data->m_executionContext.revisionConfig;
            input.nonce = m_data->m_nonce;
            input.gasPrice = protocol::effectiveGasPrice(m_data->m_transaction.get());
            input.contextID = m_data->m_contextID;
            input.seq = m_data->m_seq;
            input.nestedSeq = std::addressof(m_data->m_seq);
            input.origin = m_data->m_origin;
            input.web3Tx = m_data->m_transaction.get().type() != 0;
            input.persistContractCreateNonce = [this](const evmc_address& addr, uint64_t newNonce) {
                ledger::account::EVMAccount account(m_data->m_rollbackableStorage, addr,
                    m_data->m_executionContext.revisionConfig.use_raw_address);
                task::syncWait([](decltype(account) account, uint64_t nonce) -> task::Task<void> {
                    if (!co_await account.exists())
                    {
                        co_await account.create();
                    }
                    co_await account.setNonce(bcos::u256(nonce).str());
                }(std::move(account), newNonce));
            };
            input.web3TypedTxKind = m_data->m_web3AccessListResolved.web3TypedTxKind;
            input.accessList = m_data->m_web3AccessListResolved.accessList;
            input.txHash = m_data->m_transaction.get().hash();

            bcos::evm::state::FiscoStateView stateView(m_data->m_rollbackableStorage,
                m_data->m_executionContext.revisionConfig.use_raw_address,
                *m_data->m_executor.get().m_hashImpl);
            input.stateView = std::addressof(stateView);

            transaction_executor::FiscoPortAdapterContext portCtx{m_data->m_rollbackableStorage,
                m_data->m_blockHeader.get(), m_data->m_ledgerConfig.get(), m_data->m_origin,
                m_data->m_contextID, m_data->m_seq, m_data->m_executionContext.revisionConfig,
                *m_data->m_executor.get().m_hashImpl,
                m_data->m_executor.get().m_precompiledManager.get()};

            transaction_executor::ExecutorAuthAdapter authAdapter{portCtx};
            transaction_executor::ExecutorPrecompileAdapter precompileAdapter{portCtx};

            input.authPort = &authAdapter;
            input.chainDispatchPort = &precompileAdapter;

            co_return co_await applyFiscoMessage(std::move(input));
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        TRANSACTION_EXECUTOR_LOG(TRACE) << "Create transaction context: " << transaction;
        auto executeContext = ExecuteContext<std::decay_t<decltype(storage)>>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
        executeContext.m_data->m_executionContext.message = newEVMCMessage(blockHeader.number(),
            transaction, executeContext.m_data->m_gasLimit, executeContext.m_data->m_origin);
        co_return executeContext;
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call);

        co_await executeContext.template executeStep<0>();
        co_await executeContext.template executeStep<1>();
        co_return co_await executeContext.template executeStep<2>();
    }
};

}  // namespace bcos::evm

namespace bcos::executor_v1
{
using TransactionExecutorImpl = bcos::evm::TransactionExecutorImpl<>;
}  // namespace bcos::executor_v1
