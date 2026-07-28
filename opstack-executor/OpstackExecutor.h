/// @file OpstackExecutor.h
/// @brief An OP Stack (Optimism L2) transaction executor based on bcos-evm/opstack.
///
/// Implements the bcos::executor_v1::TransactionExecutor concept. It is the OP
/// analogue of EthereumExecutor: instead of evmone's stock
/// validate_transaction / transition, it drives the OP-specific transition
/// pipeline in bcos-evm/opstack — opValidateFromState (L1 + operator fee
/// pre-charge, blob rejection) followed by opTransition (base/L1/operator fees
/// routed to the four OP fee vaults).
///
/// Scope (v0): NORMAL transactions only. 0x7E deposit txs are out of scope —
/// they are not carried by protocol::Transaction (no source_hash / mint /
/// is_system_tx fields) and are a block-level concern; see README.md.
///
/// Adapter reuse: the BCOS<->evmone conversions, the storage-backed StateView,
/// the state-diff writeback and the receipt conversion are shared verbatim with
/// EthereumExecutor via ethereum-executor/BCOS2Evmone.h and
/// ethereum-executor/StorageStateView.h. This module therefore DEPENDS ON the
/// ethereum-executor module (PR #5366). See README.md for the merge ordering.

#pragma once

#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpReceipt.h"
#include "bcos-evm/opstack/OpTransition.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "ethereum-executor/BCOS2Evmone.h"
#include "ethereum-executor/StorageStateView.h"
#include <bcos-utilities/Exceptions.h>
#include <evmone/evmone.h>
#include <evmc/evmc.hpp>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <variant>

namespace bcos::executor_v1::opstack
{

DERIVE_BCOS_EXCEPTION(EvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(OpForkRevisionMismatch);
DERIVE_BCOS_EXCEPTION(OpTxValidationFailed);

class OpstackExecutor
{
public:
    /// @param receiptFactory produces the BCOS receipt from the evmone receipt.
    /// @param hashImpl        keccak used by the state-diff writeback.
    /// @param forkConfig      the active OP fork. v0 takes it explicitly (default: the latest
    ///        real-adapted fork, Jovian) because LedgerConfig does not yet expose an OP fork
    ///        schedule; timestamp-driven fork selection is a follow-up tied to the Karst
    ///        adaptation. The config is a reference into a static singleton
    ///        (ecotoneConfig()/.../jovianConfig()), so storing a const& is safe.
    OpstackExecutor(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl,
        bcos::evm::opstack::OpForkConfig const& forkConfig = bcos::evm::opstack::jovianConfig())
      : m_receiptFactory(receiptFactory),
        m_hashImpl(std::move(hashImpl)),
        m_forkConfig(forkConfig),
        m_vm(evmc_create_evmone())
    {}

    /// Execute a single OP Stack normal transaction.
    ///
    /// Flow (mirrors the non-deposit arm of processOpBlock, adapted to the current
    /// opValidateFromState/opTransition signatures):
    ///   1. Resolve the EVM revision from the ledger; assert it matches the fork's revision.
    ///   2. Convert BCOS block header + transaction to evmone types.
    ///   3. Build a storage-backed read-only StateView.
    ///   4. opValidateFromState: reads OP_L1_BLOCK fee params from state, prices L1 + operator
    ///      fee, applies OP-specific checks (blob rejection, gasFeeCap balance cap). Throws on
    ///      validation failure — like op-geth, a normal tx that fails pre-check has no
    ///      failed-receipt mechanism.
    ///   5. opTransition: executes and routes base/L1/operator fees to the four OP vaults. All
    ///      fee inputs come from the validate-time snapshot (props), so validate and transition
    ///      price the tx identically.
    ///   6. Apply the returned StateDiff back to storage.
    ///   7. Convert the (base) evmone receipt to a BCOS receipt.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        (void)contextID;  // single-tx executor: no cross-tx context
        (void)call;       // v0: real execution only

        namespace op = bcos::evm::opstack;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;

        // The OP fork determines the EVM revision the transition runs at; a mismatch with the
        // ledger's configured revision would silently diverge from consensus.
        if (m_forkConfig.rev != rev)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));

        auto blockInfo = eth::blockHeaderToBlockInfo(blockHeader, ledgerConfig);
        auto evmTx = eth::bcosTransactionToEvmone(transaction);

        eth::StorageStateView<Storage> stateView(storage);

        // OP L1 data fee is priced over the raw signed Ethereum tx envelope. Web3 txs carry it in
        // extraTransactionBytes(); native FISCO txs carry none, so the envelope is empty and
        // computeL1Cost returns 0 (RollupCost.h) — L1 fee simply does not apply to them.
        auto envRef = transaction.extraTransactionBytes();
        evmc::bytes_view env{envRef.data(), envRef.size()};

        uint64_t const chainId = evmTx.chain_id;
        // Single-tx execution: the whole block gas budget is available to this tx.
        int64_t const blockGasLeft = blockInfo.gas_limit;

        auto validated =
            op::opValidateFromState(stateView, blockInfo, evmTx, env, m_forkConfig, blockGasLeft);
        if (auto const* err = std::get_if<std::error_code>(&validated))
            BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(err->message()));
        auto const& props = std::get<op::OpTxProperties>(validated);

        eth::ZeroBlockHashes blockHashes;
        auto opReceipt = op::opTransition(
            stateView, blockInfo, blockHashes, evmTx, m_forkConfig, m_vm, props, chainId);

        std::map<evmc::address, std::set<evmc::bytes32>> storageTracker;
        co_await eth::applyStateDiff(
            storage, opReceipt.receipt.state_diff, rev, *m_hashImpl, storageTracker);

        // v0 emits the base (evmone) receipt. The OP receipt meta (opReceipt.meta: l1_fee,
        // operator_fee, DA footprint, ...) has no field on protocol::TransactionReceipt yet;
        // surfacing it is a receipt-extension follow-up (see README.md).
        co_return eth::evmoneReceiptToBcos(
            opReceipt.receipt, m_receiptFactory, blockHeader.number());
    }

    // ---- TransactionExecutor concept: ExecuteContext ----
    template <class Storage>
    struct ExecuteContext
    {
        OpstackExecutor& executor;
        Storage& storage;
        protocol::BlockHeader const& blockHeader;
        protocol::Transaction const& transaction;
        int contextID;
        ledger::LedgerConfig const& ledgerConfig;
        bool call;

        ExecuteContext(OpstackExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c)
          : executor(exec),
            storage(st),
            blockHeader(bh),
            transaction(tx),
            contextID(cid),
            ledgerConfig(cfg),
            call(c)
        {}
    };

    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
    }

private:
    protocol::TransactionReceiptFactory const& m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    bcos::evm::opstack::OpForkConfig const& m_forkConfig;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
