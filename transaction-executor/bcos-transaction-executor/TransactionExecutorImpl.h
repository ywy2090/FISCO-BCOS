#pragma once

#include "RollbackableStorage.h"
#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-evm/bcos/FiscoTxExecutor.h"
#include "bcos-evm/ethereum/EVMCResult.h"
#include "bcos-evm/ethereum/eip2929/Eip2929AccessState.h"
#include "bcos-evm/ethereum/gas/EthTxGasSettlement.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Exceptions.h"
#include "precompiled/PrecompiledManager.h"
#include "vm/HostContext.h"
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>

namespace bcos::executor_v1
{

enum class ExecutePhase : uint8_t
{
    Prepare = 0,
    Execute = 1,
    Finalize = 2
};

#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

DERIVE_BCOS_EXCEPTION(InvalidReceiptVersion);

evmc_message newEVMCMessage(protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin);

template <class TxExec = FiscoTxExecutor>
class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl, PrecompiledManager& precompiledManager);

    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    std::reference_wrapper<PrecompiledManager> m_precompiledManager;
    TxExec m_txExecutor;

    using TransientStorage =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

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
            TransientStorage m_transientStorage;
            Rollbackable<decltype(m_transientStorage)> m_rollbackableTransientStorage;
            bool m_call;
            int64_t m_gasUsed = 0;
            std::string m_gasPriceStr;

            bcos::chain_policy::FiscoPolicy m_policy;
            int64_t m_gasLimit;
            int64_t m_seq = 0;
            evmc_address m_origin;
            u256 m_nonce;
            executor::Web3AccessListResolved m_web3AccessListResolved;
            std::shared_ptr<executor::Eip2929AccessState> m_eip2929Access;
            hostcontext::HostContext<decltype(m_rollbackableStorage),
                decltype(m_rollbackableTransientStorage), typename TxExec::PolicyType>
                m_hostContext;
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
                m_rollbackableTransientStorage(m_transientStorage),
                m_call(call),
                m_policy(ledgerConfig.features(), ledgerConfig.balanceTransfer(),
                    ledgerConfig.authCheckStatus() != 0),
                m_gasLimit(computeEffectiveGasLimit(transaction,
                    static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit())),
                    m_policy.computeRevisionConfig(blockHeader).fix_gas_precheck)),
                m_origin((!m_transaction.get().sender().empty() &&
                             m_transaction.get().sender().size() == sizeof(evmc_address)) ?
                             *(evmc_address*)m_transaction.get().sender().data() :
                             evmc_address{}),
                m_nonce(hex2u(transaction.nonce())),
                m_web3AccessListResolved(executor::resolveWeb3AccessList(transaction)),
                m_eip2929Access(std::make_shared<executor::Eip2929AccessState>()),
                m_hostContext(m_rollbackableStorage, m_rollbackableTransientStorage, blockHeader,
                    newEVMCMessage(m_blockHeader.get().number(), transaction, m_gasLimit, m_origin),
                    m_origin, transaction.abi(), contextID, m_seq, executor.m_precompiledManager,
                    m_policy.computeRevisionConfig(blockHeader), m_policy, *executor.m_hashImpl,
                    transaction.type() != 0, m_nonce, m_web3AccessListResolved.accessList,
                    m_web3AccessListResolved.web3TypedTxKind, m_eip2929Access)
            {}
        };
        std::unique_ptr<Data> m_data;

        ExecuteContext(TransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
          : m_data(std::make_unique<Data>(
                executor, storage, blockHeader, transaction, contextID, ledgerConfig, call))
        {}

        template <ExecutePhase phase>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            if constexpr (phase == ExecutePhase::Prepare)
            {
                co_await m_data->m_hostContext.prepare();
            }
            else if constexpr (phase == ExecutePhase::Execute)
            {
                auto updated = co_await updateNonce();
                if (updated)
                    m_data->m_startSavepoint = m_data->m_rollbackableStorage.current();

                if (!m_data->m_call)
                {
                    if (!co_await m_data->m_executor.get().m_txExecutor.buyGas(*m_data))
                        co_return {};
                }
                m_data->m_hostContext.setGasSettlementGasLimit(m_data->m_gasLimit);
                m_data->m_evmcResult.emplace(co_await m_data->m_hostContext.execute());
                settleGasUsedFromEvmResult();
                if (!m_data->m_call)
                {
                    co_await m_data->m_executor.get().m_txExecutor.refundGas(*m_data);
                }
            }
            else if constexpr (phase == ExecutePhase::Finalize)
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
                    m_data->m_hostContext.revisionConfig().use_raw_address);

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
            auto const& snapOpt = m_data->m_hostContext.gasSettlementSnapshot();

            if (snapOpt &&
                m_data->m_transaction.get().type() == protocol::TransactionType::Web3Transaction &&
                m_data->m_hostContext.revisionConfig().eip7623)
            {
                auto ctx = *snapOpt;
                ctx.evmGasLeft = evmcResult.gas_left;
                ctx.evmGasRefund = evmcResult.gas_refund;
                m_data->m_gasUsed = executor_v1::gas::finalizeEthereumGasUsed(
                    ctx, m_data->m_hostContext.revisionConfig().calldata_floor_per_token);
            }
            else
            {
                m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
            }
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        TRANSACTION_EXECUTOR_LOG(TRACE) << "Create transaction context: " << transaction;
        co_return {*this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call);

        co_await executeContext.template executeStep<ExecutePhase::Prepare>();
        co_await executeContext.template executeStep<ExecutePhase::Execute>();
        co_return co_await executeContext.template executeStep<ExecutePhase::Finalize>();
    }
};

}  // namespace bcos::executor_v1
