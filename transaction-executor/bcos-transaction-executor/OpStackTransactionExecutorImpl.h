#pragma once

#include "OpStackTxInputBuilder.h"
#include "RollbackableStorage.h"
#include "bcos-evm/bcos/FiscoStateView.h"
#include "bcos-evm/bcos/StateDiffApplier.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/opstack/OpStackTxExecutor.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <boost/algorithm/hex.hpp>
#include <memory>
#include <span>

namespace bcos::evm
{

enum class OpStackExecutePhase : uint8_t
{
    Prepare = 0,
    Execute = 1,
    Finalize = 2
};

#define OPSTACK_TRANSACTION_EXECUTOR_LOG(LEVEL) \
    BCOS_LOG(LEVEL) << LOG_BADGE("OPSTACK_TRANSACTION_EXECUTOR")

// Reuse message builder from TransactionExecutorImpl.cpp.
evmc_message newEVMCMessage(bcos::protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin);

namespace opstack_executor_detail
{
inline std::vector<protocol::LogEntry> convertLogs(std::vector<LogEntry> const& logs)
{
    std::vector<protocol::LogEntry> out;
    out.reserve(logs.size());
    for (auto const& log : logs)
    {
        std::span addressView(log.address.bytes, sizeof(log.address.bytes));
        h256s topics;
        topics.reserve(log.topics.size());
        for (auto const& topic : log.topics)
        {
            topics.emplace_back(state::fromEvmC(topic));
        }
        out.emplace_back(
            toHex<decltype(addressView), bcos::bytes>(addressView), std::move(topics), log.data);
    }
    return out;
}
}  // namespace opstack_executor_detail

/// Isthmus OP-Stack transaction executor — integrates `opStackExecuteViaHost` with
/// baseline scheduler / Engine API via the executor_v1::TransactionExecutor concept.
///
/// Compared to TransactionExecutorImpl:
/// - No FISCO auth/precompile hooks (L1Block native dispatch via OpHostExtension).
/// - Gas buy/refund/settlement is internal to opStackExecuteViaHost.
/// - Uses gasTipCap/gasFeeCap (EIP-1559) instead of legacy gasPrice.
class OpStackTransactionExecutorImpl
{
public:
    explicit OpStackTransactionExecutorImpl(
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
            std::reference_wrapper<OpStackTransactionExecutorImpl> m_executor;
            std::reference_wrapper<protocol::BlockHeader const> m_blockHeader;
            std::reference_wrapper<protocol::Transaction const> m_transaction;
            int m_contextID;
            std::reference_wrapper<ledger::LedgerConfig const> m_ledgerConfig;
            Rollbackable<Storage> m_rollbackableStorage;
            Rollbackable<Storage>::Savepoint m_startSavepoint;
            bool m_call;
            int64_t m_gasUsed{0};
            bcos::u256 m_effectiveGasPrice{0};
            int64_t m_gasLimit;
            evmc_address m_origin{};
            uint64_t m_nonce{0};
            evmc::VM m_vm;
            std::optional<EVMCResult> m_evmcResult;
            OpStackReceiptMeta m_receiptMeta;
            std::vector<protocol::LogEntry> m_logs;
            std::shared_ptr<opstack_tx::BlockGasPool> m_blockGasPool;

            Data(OpStackTransactionExecutorImpl& executor, Storage& storage,
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
                m_nonce(hex2u(transaction.nonce()).convert_to<uint64_t>()),
                m_vm(evmc_create_evmone()),
                m_blockGasPool(std::make_shared<opstack_tx::BlockGasPool>(
                    static_cast<int64_t>(std::get<0>(ledgerConfig.gasLimit()))))
            {}
        };
        std::unique_ptr<Data> m_data;

        ExecuteContext(OpStackTransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
          : m_data(std::make_unique<Data>(
                executor, storage, blockHeader, transaction, contextID, ledgerConfig, call))
        {}

        template <int phase>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            if constexpr (phase == static_cast<int>(OpStackExecutePhase::Prepare))
            {
                // OP path has no separate prepare; preCheck/warm live in opStackExecuteViaHost.
            }
            else if constexpr (phase == static_cast<int>(OpStackExecutePhase::Execute))
            {
                auto output = co_await opStackExecuteViaHostTx();
                m_data->m_evmcResult.emplace(std::move(output.evmcResult));
                m_data->m_gasUsed = output.gasUsed;
                m_data->m_receiptMeta = output.receiptMeta;
                m_data->m_logs = opstack_executor_detail::convertLogs(output.logs);
                m_data->m_effectiveGasPrice = resolveEffectiveGasPrice(
                    opstack_tx::parseU256Field(m_data->m_transaction.get().maxPriorityFeePerGas()),
                    opstack_tx::parseU256Field(m_data->m_transaction.get().maxFeePerGas()),
                    opstack_tx::resolveOpStackBaseFee(m_data->m_ledgerConfig.get()));

                if (m_data->m_evmcResult->status_code == EVMC_SUCCESS ||
                    m_data->m_evmcResult->status_code == EVMC_REVERT)
                {
                    co_await state::applyStateDiff(m_data->m_rollbackableStorage, output.stateDiff,
                        false, *m_data->m_executor.get().m_hashImpl,
                        m_data->m_transaction.get().abi());
                }
            }
            else if constexpr (phase == static_cast<int>(OpStackExecutePhase::Finalize))
            {
                co_return co_await makeReceipt();
            }
            co_return {};
        }

        task::Task<OpStackExecuteViaHostOutput> opStackExecuteViaHostTx()
        {
            evmc_message message = newEVMCMessage(m_data->m_blockHeader.get().number(),
                m_data->m_transaction.get(), m_data->m_gasLimit, m_data->m_origin);

            state::FiscoStateView stateView(
                m_data->m_rollbackableStorage, false, *m_data->m_executor.get().m_hashImpl);

            OpStackExecuteViaHostInput input;
            input.stateView = std::addressof(stateView);
            input.vm = std::addressof(m_data->m_vm);
            input.hashImpl = m_data->m_executor.get().m_hashImpl.get();
            input.message = message;
            input.nonce = m_data->m_nonce;
            input.call = m_data->m_call;
            input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
            auto const baseFee = opstack_tx::resolveOpStackBaseFee(m_data->m_ledgerConfig.get());
            auto const blobBaseFee = opstack_tx::resolveOpStackBlobBaseFee(stateView);
            input.blockInfo = opstack_tx::buildOpStackBlockInfo(
                m_data->m_blockHeader.get(), m_data->m_ledgerConfig.get(), baseFee, blobBaseFee);
            input.blockHashes = state::buildFiscoBlockHashes(
                m_data->m_rollbackableStorage, m_data->m_blockHeader.get().number());
            opstack_tx::fillGasCaps(m_data->m_transaction.get(), input);
            opstack_tx::fillWeb3Fields(m_data->m_transaction.get(), input);
            opstack_tx::applyDefaultTxProps(input);
            input.rollupCostData = opstack_tx::buildRollupCostData(m_data->m_transaction.get());
            input.gasPoolSubGasHook = [pool = m_data->m_blockGasPool](
                                          uint64_t gas) { return !pool || pool->tryConsume(gas); };
            input.opTxExecutor.m_isIsthmus =
                true;  // Isthmus executor always activates operator fee

            co_return co_await opStackExecuteViaHost(std::move(input));
        }

        task::Task<protocol::TransactionReceipt::Ptr> makeReceipt()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            std::string newContractAddress;
            if (evmcResult.create_address.bytes[0] != 0 || evmcResult.create_address.bytes[19] != 0)
            {
                if (evmcResult.status_code == EVMC_SUCCESS)
                {
                    newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
                    boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                        evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                        std::back_inserter(newContractAddress));
                }
            }

            auto receiptStatus = static_cast<int32_t>(evmcResult.status);
            auto gasPriceStr = "0x" + m_data->m_effectiveGasPrice.str(0, std::ios_base::hex);
            auto const transactionVersion = static_cast<bcos::protocol::TransactionVersion>(
                m_data->m_transaction.get().version());

            protocol::TransactionReceipt::Ptr receipt;
            switch (transactionVersion)
            {
            case bcos::protocol::TransactionVersion::V0_VERSION:
                receipt = m_data->m_executor.get().m_receiptFactory.get().createReceipt(
                    m_data->m_gasUsed, std::move(newContractAddress), m_data->m_logs, receiptStatus,
                    {evmcResult.output_data, evmcResult.output_size},
                    m_data->m_blockHeader.get().number());
                break;
            case bcos::protocol::TransactionVersion::V1_VERSION:
            case bcos::protocol::TransactionVersion::V2_VERSION:
                receipt = m_data->m_executor.get().m_receiptFactory.get().createReceipt2(
                    m_data->m_gasUsed, std::move(newContractAddress), m_data->m_logs, receiptStatus,
                    {evmcResult.output_data, evmcResult.output_size},
                    m_data->m_blockHeader.get().number(), std::move(gasPriceStr),
                    transactionVersion);
                break;
            default:
                BOOST_THROW_EXCEPTION(std::runtime_error("Invalid receipt version"));
            }
            receipt->setEffectiveGasPrice(gasPriceStr);
            if (m_data->m_receiptMeta.l1Fee.has_value())
            {
                receipt->setL1Fee("0x" + m_data->m_receiptMeta.l1Fee->str(0, std::ios_base::hex));
            }
            if (m_data->m_receiptMeta.operatorFee.has_value())
            {
                receipt->setOperatorFee(
                    "0x" + m_data->m_receiptMeta.operatorFee->str(0, std::ios_base::hex));
            }
            if (m_data->m_receiptMeta.depositNonce.has_value())
            {
                auto const nonce = bcos::u256(*m_data->m_receiptMeta.depositNonce);
                receipt->setDepositNonce("0x" + nonce.str(0, std::ios_base::hex));
            }
            co_return receipt;
        }
    };

    template <class Storage>
    auto createExecuteContext(Storage& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call) -> task::Task<ExecuteContext<Storage>>
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
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
using OpStackTransactionExecutorImpl = bcos::evm::OpStackTransactionExecutorImpl;
}  // namespace bcos::executor_v1
