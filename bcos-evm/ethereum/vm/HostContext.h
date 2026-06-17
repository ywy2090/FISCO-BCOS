/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief host context
 * @file HostContext.h
 * @author: ancelmo
 * @date: 2022-12-24
 */

#pragma once

#include "RevisionConfig.h"
#include "VMInstance.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/AuthCheck.h"
#include "bcos-evm/bcos/PrecompiledImpl.h"
#include "bcos-evm/bcos/PrecompiledManager.h"
#include "bcos-evm/ethereum/eip2929/Eip2929AccessState.h"
#include "bcos-evm/ethereum/eip2929/Eip2929CheckpointGuard.h"
#include "bcos-evm/ethereum/eip2929/Eip2929TransactionPrewarm.h"
#include "bcos-evm/ethereum/eip2929/Eip2929Util.h"
#include "bcos-evm/ethereum/vm/VMInstance.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-transaction-executor/EVMCResult.h"
#include "bcos-transaction-executor/gas/EthTxGasSettlement.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmone/evmone.h>
#include <boost/algorithm/hex.hpp>
#include <boost/concept_archetype.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <boost/throw_exception.hpp>
#include <cassert>
#include <evmc/evmc.hpp>  // evmc::HostInterface, evmc::Result
#include <functional>
#include <intx/intx.hpp>
#include <iterator>
#include <memory>
#include <optional>
#include <range/v3/algorithm/equal.hpp>
#include <range/v3/algorithm/fill.hpp>
#include <range/v3/algorithm/move.hpp>
#include <string_view>

namespace bcos::executor_v1::hostcontext
{

#define HOST_CONTEXT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("HOST_CONTEXT")

// clang-format off
struct NotFoundCodeError : public bcos::Error {};
// clang-format on

evmc_bytes32 evm_hash_fn(evmc_host_context* context, const uint8_t* data, size_t size);

struct Executable
{
    Executable(storage::Entry code);
    Executable(bytesConstRef code);

    std::optional<storage::Entry> m_code;
    VMInstance m_vmInstance;
};

template <class Storage>
using Account = ledger::account::EVMAccount<Storage>;

using CacheExecutables =
    storage2::memory_storage::MemoryStorage<evmc_address, std::shared_ptr<Executable>,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT),
        std::hash<evmc_address>>;
CacheExecutables& getCacheExecutables();

task::Task<std::shared_ptr<Executable>> getExecutable(
    auto& storage, const evmc_address& address, const evmc_revision& revision, bool binaryAddress)
{
    if (auto executable = co_await storage2::readOne(getCacheExecutables(), address))
    {
        co_return std::move(*executable);
    }

    if (Account<std::decay_t<decltype(storage)>> account(storage, address, binaryAddress);
        auto codeEntry = co_await account.code())
    {
        auto executable = std::make_shared<Executable>(std::move(*codeEntry));
        co_await storage2::writeOne(getCacheExecutables(), address, executable);
        co_return executable;
    }
    co_return {};
}

}  // namespace bcos::executor_v1::hostcontext

namespace bcos::evm_standard::detail
{
constexpr evmc_host_interface s_hostTable = {
    [](evmc_host_context* c, const evmc_address* a) noexcept -> bool {
        return static_cast<evmc::HostInterface*>(c)->account_exists(*a);
    },
    [](evmc_host_context* c, const evmc_address* a, const evmc_bytes32* k) noexcept
    -> evmc_bytes32 { return static_cast<evmc::HostInterface*>(c)->get_storage(*a, *k); },
    [](evmc_host_context* c, const evmc_address* a, const evmc_bytes32* k,
        const evmc_bytes32* v) noexcept -> evmc_storage_status {
        return static_cast<evmc::HostInterface*>(c)->set_storage(*a, *k, *v);
    },
    [](evmc_host_context* c, const evmc_address* a) noexcept -> evmc_bytes32 {
        return static_cast<evmc::HostInterface*>(c)->get_balance(*a);
    },
    [](evmc_host_context* c, const evmc_address* a) noexcept -> size_t {
        return static_cast<evmc::HostInterface*>(c)->get_code_size(*a);
    },
    [](evmc_host_context* c, const evmc_address* a) noexcept -> evmc_bytes32 {
        return static_cast<evmc::HostInterface*>(c)->get_code_hash(*a);
    },
    [](evmc_host_context* c, const evmc_address* a, size_t off, uint8_t* buf, size_t sz) noexcept
    -> size_t { return static_cast<evmc::HostInterface*>(c)->copy_code(*a, off, buf, sz); },
    [](evmc_host_context* c, const evmc_address* a, const evmc_address* b) noexcept -> bool {
        return static_cast<evmc::HostInterface*>(c)->selfdestruct(*a, *b);
    },
    [](evmc_host_context* c, const evmc_message* m) noexcept -> evmc_result {
        auto result = static_cast<evmc::HostInterface*>(c)->call(*m);
        evmc_result raw = result;
        result.release = nullptr;
        return raw;
    },
    [](evmc_host_context* c) noexcept -> evmc_tx_context {
        return static_cast<evmc::HostInterface*>(c)->get_tx_context();
    },
    [](evmc_host_context* c, int64_t n) noexcept -> evmc_bytes32 {
        return static_cast<evmc::HostInterface*>(c)->get_block_hash(n);
    },
    [](evmc_host_context* c, const evmc_address* a, const uint8_t* d, size_t ds,
        const evmc_bytes32* t, size_t nt) noexcept -> void {
        static_cast<evmc::HostInterface*>(c)->emit_log(*a, d, ds, t, nt);
    },
    [](evmc_host_context* c, const evmc_address* a) noexcept -> evmc_access_status {
        return static_cast<evmc::HostInterface*>(c)->access_account(*a);
    },
    [](evmc_host_context* c, const evmc_address* a, const evmc_bytes32* k) noexcept
    -> evmc_access_status { return static_cast<evmc::HostInterface*>(c)->access_storage(*a, *k); },
    [](evmc_host_context* c, const evmc_address* a, const evmc_bytes32* k) noexcept
    -> evmc_bytes32 { return static_cast<evmc::HostInterface*>(c)->get_transient_storage(*a, *k); },
    [](evmc_host_context* c, const evmc_address* a, const evmc_bytes32* k,
        const evmc_bytes32* v) noexcept -> void {
        static_cast<evmc::HostInterface*>(c)->set_transient_storage(*a, *k, *v);
    },
};
}  // namespace bcos::evm_standard::detail

namespace bcos::executor_v1::hostcontext
{

template <class Storage, class TransientStorage, class Policy>
class HostContext : public evmc_host_context, public evmc::HostInterface
{
private:
    std::reference_wrapper<Storage> m_rollbackableStorage;
    std::reference_wrapper<TransientStorage> m_rollbackableTransientStorage;
    std::reference_wrapper<const protocol::BlockHeader> m_blockHeader;
    std::reference_wrapper<const evmc_address> m_origin;
    std::string_view m_abi;
    int m_contextID;
    std::reference_wrapper<int64_t> m_seq;
    std::reference_wrapper<const PrecompiledManager> m_precompiledManager;
    std::reference_wrapper<const ledger::LedgerConfig> m_ledgerConfig;
    std::reference_wrapper<const crypto::Hash> m_hashImpl;
    evmc_message m_message;
    Account<Storage> m_recipientAccount;

    evmc_revision m_revision;
    std::vector<protocol::LogEntry> m_logs;
    std::shared_ptr<Executable> m_executable;
    const bcos::executor_v1::Precompiled* m_preparedPrecompiled{};
    bcos::bytes m_dynamicPrecompiledInput;
    bool m_enableTransfer = false;
    int64_t m_level;
    bool m_web3Tx;

    // EIP-2929/2930 access tracking (shared Eip2929AccessState; see accessAccount/accessStorage)
    std::shared_ptr<executor::Eip2929AccessState> m_eip2929Access;
    std::shared_ptr<const executor::Eip2930AccessList> m_eip2930AccessList;
    uint8_t m_web3TypedTxKindForAccessList = 0;

    bcos::evm_standard::RevisionConfig m_rev;  // Pre-computed per-block revision config
    Policy* m_policy = nullptr;                // Chain-variant behavior hooks

    std::optional<executor_v1::gas::TxGasSettlementContext> m_gasSettlementSnapshot;
    int64_t m_gasSettlementGasLimit = 0;

    void captureGasSettlementSnapshotBeforeEvm()
    {
        if (!(m_level == 0 && m_web3Tx && m_rev.eip7623))
        {
            return;
        }
        auto const& msg = message();
        auto const components =
            executor::calcEip7623Components(bcos::bytesConstRef(msg.input_data, msg.input_size));
        auto const intrinsic = executor_v1::gas::computeTxIntrinsicGas(
            msg, m_eip2930AccessList.get(), m_web3TypedTxKindForAccessList);

        executor_v1::gas::TxGasSettlementContext snap{};
        snap.gasLimit = m_gasSettlementGasLimit;
        snap.gasBeforeEvm = msg.gas;
        snap.calldata = components;
        snap.fixedIntrinsic = intrinsic.fixedCost();
        snap.createTerm = intrinsic.createIntrinsic;
        m_gasSettlementSnapshot = snap;
    }

    constexpr auto buildLegacyExternalCaller()
    {
        return
            [this](const evmc_message& message) { return task::syncWait(externalCall(message)); };
    }

    task::Task<void> transferBalance(const evmc_message& message)
    {
        if (message.code_address == message.sender || message.recipient == message.sender)
        {
            co_return;
        }

        auto value = fromEvmC(message.value);
        auto senderAccount = getAccount(*this, message.sender);
        auto fromBalance = co_await senderAccount.balance();

        if (fromBalance < value)
        {
            HOST_CONTEXT_LOG(DEBUG) << m_blockHeader.get().number() << " "
                                    << LOG_BADGE("AccountPrecompiled, subAccountBalance")
                                    << LOG_DESC("account balance not enough");
            BOOST_THROW_EXCEPTION(protocol::NotEnoughCashError{}
                                  << errinfo_comment("Account balance is not enough!"));
        }

        if (!co_await m_recipientAccount.exists())
        {
            co_await m_recipientAccount.create();
        }

        auto toBalance = co_await m_recipientAccount.balance();
        co_await senderAccount.setBalance(fromBalance - value);
        co_await m_recipientAccount.setBalance(toBalance + value);
    }

    constexpr static struct InnerConstructor
    {
    } innerConstructor{};

    HostContext(InnerConstructor /*unused*/, Storage& storage, TransientStorage& transientStorage,
        const protocol::BlockHeader& blockHeader, const evmc_message& message,
        const evmc_address& origin, std::string_view abi, int contextID, int64_t& seq,
        PrecompiledManager const& precompiledManager, bcos::evm_standard::RevisionConfig rev,
        Policy& policy, crypto::Hash const& hashImpl, bool web3Tx, const u256& nonce,
        std::shared_ptr<const executor::Eip2930AccessList> accessList = nullptr,
        uint8_t web3TypedTxKind = 0,
        std::shared_ptr<executor::Eip2929AccessState> eip2929Access = nullptr)
      : evmc_host_context{.interface = &bcos::evm_standard::detail::s_hostTable,
            .wasm_interface = nullptr,
            .hash_fn = evm_hash_fn,
            .isSMCrypto = (hashImpl.getHashImplType() == crypto::HashImplType::Sm3Hash),
            .version = 0,
            .metrics = std::addressof(executor::ethMetrics)},
        m_rollbackableStorage(storage),
        m_rollbackableTransientStorage(transientStorage),
        m_blockHeader(blockHeader),
        m_origin(origin),
        m_abi(abi),
        m_contextID(contextID),
        m_seq(seq),
        m_precompiledManager(precompiledManager),
        m_rev(rev),
        m_policy(&policy),
        m_hashImpl(hashImpl),
        m_message(Policy::deriveMessage(
            web3Tx, message, m_blockHeader.get().number(), m_contextID, m_seq, nonce, m_hashImpl)),
        m_recipientAccount(getAccount(*this, this->message().recipient)),
        // Never downgrade below EVMC_CANCUN (pre-PR baseline).
        // Future EVM feature flags (Amsterdam, etc.) automatically upgrade through
        // toRevision without requiring code changes here.
        m_revision(
            std::max(bcos::executor::toRevision(ledgerConfig.features(), blockHeader.version()),
                EVMC_CANCUN)),
        m_level(seq),
        m_web3Tx(web3Tx),
        m_eip2929Access(std::move(eip2929Access)),
        m_eip2930AccessList(std::move(accessList)),
        m_web3TypedTxKindForAccessList(web3TypedTxKind)
    {
        // W1 warm at top-level construction (sync). Nested HostContext (m_level>0) skips.
        // prepare() handles prepareCall/Create only; see TransactionExecutorImpl executeStep<0>.
        warmEip2929AtTransactionEntry();
        assert(!executor::eip2929Enabled(m_rev) || m_eip2929Access);
    }

public:
    HostContext(Storage& storage, TransientStorage& transientStorage,
        protocol::BlockHeader const& blockHeader, const evmc_message& message,
        const evmc_address& origin, std::string_view abi, int contextID, int64_t& seq,
        PrecompiledManager const& precompiledManager, bcos::evm_standard::RevisionConfig rev,
        Policy& policy, crypto::Hash const& hashImpl, bool web3Tx, const u256& nonce,
        std::shared_ptr<const executor::Eip2930AccessList> accessList = nullptr,
        uint8_t web3TypedTxKind = 0,
        std::shared_ptr<executor::Eip2929AccessState> eip2929Access = nullptr)
      : HostContext(innerConstructor, storage, transientStorage, blockHeader, message, origin, abi,
            contextID, seq, precompiledManager, rev, policy, hashImpl, web3Tx, nonce,
            std::move(accessList), web3TypedTxKind, std::move(eip2929Access))
    {}

    ~HostContext() noexcept = default;
    HostContext(HostContext const&) = default;
    HostContext& operator=(HostContext const&) = default;
    HostContext(HostContext&&) noexcept = default;
    HostContext& operator=(HostContext&&) noexcept = default;

    constexpr evmc_message const& message() const& { return m_message; }
    constexpr evmc_message& mutableMessage() & { return m_message; }
    constexpr ledger::LedgerConfig const& ledgerConfig() const& { return m_ledgerConfig.get(); }
    Policy& policy() const { return *m_policy; }
    const bcos::evm_standard::RevisionConfig& revisionConfig() const { return m_rev; }

    friend auto getAccount(HostContext& hostContext, const evmc_address& address)
    {
        return Account<std::decay_t<Storage>>(
            hostContext.m_rollbackableStorage.get(), address, hostContext.m_rev.use_raw_address);
    }

    task::Task<evmc_bytes32> get(const evmc_bytes32* key, auto&&... /*unused*/)
    {
        co_return co_await m_recipientAccount.storage(*key);
    }

    // DIRECT-tagged variant: reads the underlying slot without populating the
    // ReadWriteSetStorage read set. Use only for internal metadata reads (e.g.
    // SSTORE status determination) that must not influence DAG conflict edges.
    task::Task<evmc_bytes32> get(const evmc_bytes32* key, storage2::DIRECT_TYPE direct)
    {
        co_return co_await m_recipientAccount.storage(*key, direct);
    }

    task::Task<void> set(const evmc_bytes32* key, const evmc_bytes32* value, auto&&... /*unused*/)
    {
        co_await m_recipientAccount.setStorage(*key, *value);
    }

    task::Task<u256> balance(evmc_address addr, auto&&... /*unused*/)
    {
        auto account = getAccount(*this, addr);
        co_return co_await account.balance();
    }

    task::Task<evmc_bytes32> getTransientStorage(const evmc_bytes32* key, auto&&... /*unused*/)
    {
        evmc_bytes32 value;
        if (auto valueEntry = co_await storage2::readOne(m_rollbackableTransientStorage.get(),
                executor_v1::StateKeyView{
                    concepts::bytebuffer::toView(co_await m_recipientAccount.path()),
                    concepts::bytebuffer::toView(key->bytes)}))
        {
            auto field = valueEntry->get();
            std::uninitialized_copy_n(field.data(), sizeof(value), value.bytes);
        }
        else
        {
            ::ranges::fill(value.bytes, 0);
        }
        HOST_CONTEXT_LOG(TRACE) << "getTransientStorage:"
                                << LOG_KV("key", concepts::bytebuffer::toView(key->bytes))
                                << LOG_KV("value", concepts::bytebuffer::toView(value.bytes));
        co_return value;
    }

    task::Task<void> setTransientStorage(
        const evmc_bytes32* key, const evmc_bytes32* value, auto&&... /*unused*/)
    {
        storage::Entry valueEntry(concepts::bytebuffer::toView(value->bytes));
        StateKey stateKey{concepts::bytebuffer::toView(co_await m_recipientAccount.path()),
            concepts::bytebuffer::toView(key->bytes)};
        HOST_CONTEXT_LOG(TRACE) << "setTransientStorage:"
                                << LOG_KV("key", concepts::bytebuffer::toView(key->bytes));
        co_await storage2::writeOne(
            m_rollbackableTransientStorage.get(), std::move(stateKey), std::move(valueEntry));
    }

    task::Task<std::optional<storage::Entry>> code(
        const evmc_address& address, auto&&... /*unused*/)
    {
        if (auto executable = co_await getExecutable(
                m_rollbackableStorage.get(), address, m_revision, m_rev.use_raw_address);
            executable && executable->m_code)
        {
            co_return executable->m_code;
        }
        co_return {};
    }

    task::Task<size_t> codeSizeAt(const evmc_address& address, auto&&... /*unused*/)
    {
        if (auto const* precompiled =
                m_precompiledManager.get().getPrecompiled(address, m_rev, m_policy->features()))
        {
            co_return executor_v1::size(*precompiled);
        }

        if (auto codeEntry = co_await code(address))
        {
            co_return codeEntry->get().size();
        }
        co_return 0;
    }

    task::Task<h256> codeHashAt(const evmc_address& address, auto&&... /*unused*/)
    {
        Account<Storage> account(m_rollbackableStorage.get(), address, m_rev.use_raw_address);
        co_return co_await account.codeHash();
    }

    task::Task<bool> exists([[maybe_unused]] const evmc_address& address, auto&&... /*unused*/)
    {
        // TODO: impl the full support for solidity
        co_return true;
    }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    task::Task<h256> blockHash(int64_t number, auto&&... /*unused*/) const
    {
        if (number < blockNumber() && number >= 0)
        {
            if (auto blockHash = co_await ledger::getBlockHash(
                    m_rollbackableStorage.get(), number, ledger::fromStorage))
            {
                co_return *blockHash;
            }
        }
        co_return {};
    }
    int64_t blockNumber() const { return m_blockHeader.get().number(); }
    uint32_t blockVersion() const { return m_blockHeader.get().version(); }
    int64_t timestamp() const
    {
        return m_policy->convertTimestamp(m_blockHeader.get().timestamp());
    }
    evmc_address const& origin() const { return m_origin; }
    int64_t blockGasLimit() const { return std::get<0>(m_ledgerConfig.get().gasLimit()); }
    u256 gasPrice() const { return u256(std::get<0>(m_ledgerConfig.get().gasPrice())); }
    evmc_uint256be chainId() const
    {
        return m_ledgerConfig.get().chainId().value_or(evmc_uint256be{});
    }

    evmc_revision revision() const { return m_revision; }

    void setGasSettlementGasLimit(int64_t gasLimit) { m_gasSettlementGasLimit = gasLimit; }

    std::optional<executor_v1::gas::TxGasSettlementContext> const& gasSettlementSnapshot() const
    {
        return m_gasSettlementSnapshot;
    }

    /// Revert any changes made (by any of the other calls).
    void log(const evmc_address& address, h256s topics, bytesConstRef data)
    {
        std::span view(address.bytes);
        m_logs.emplace_back(
            toHex<decltype(view), bcos::bytes>(view), std::move(topics), data.toBytes());
    }

    void suicide()
    {
        // suicide(m_myContractTable); // TODO: add suicide
    }

    task::Task<void> prepare()
    {
        auto const& ref = message();

        if (ref.kind == EVMC_CREATE || ref.kind == EVMC_CREATE2)
        {
            prepareCreate();
            co_return;
        }
        co_await prepareCall();
    }

    task::Task<EVMCResult> execute()
    {
        const auto* ref = std::addressof(message());
        HOST_CONTEXT_LOG(TRACE) << "HostContext execute level: " << m_level << " " << *ref;

        auto savepoint = m_rollbackableStorage.get().current();
        auto transientSavepoint = m_rollbackableTransientStorage.get().current();

        std::optional<bcos::executor::Eip2929CheckpointGuard> topCheckpointGuard;
        if (m_level == 0 && m_eip2929Access && executor::eip2929Enabled(m_rev))
        {
            topCheckpointGuard.emplace(m_eip2929Access);
        }

        std::optional<EVMCResult> evmResult;
        // FIB-76~92 (bugfix_v1_error_handling): read once, gates all receipt-affecting
        // error paths below for hard-fork compat
        const bool fixErrorHandling = m_rev.fix_error_handling;
        try
        {
            // FIB-91: checkAuth() moved inside try block so exceptions
            // trigger rollback/cleanup instead of bypassing it
            if (m_rev.enable_auth_check)
            {
                HOST_CONTEXT_LOG(DEBUG)
                    << "Checking auth..." << m_ledgerConfig.get().authCheckStatus()
                    << " gas: " << ref->gas;

                if constexpr (bcos::evm_standard::HasAuthCheck<Policy,
                                  decltype(m_rollbackableStorage.get()),
                                  const protocol::BlockHeader&, const evmc_message&,
                                  const evmc_address&, decltype(buildLegacyExternalCaller()),
                                  const PrecompiledManager&, int64_t, int64_t, const crypto::Hash&>)
                {
                    if (auto result = m_policy->checkAuth(m_rollbackableStorage.get(),
                            m_blockHeader, *ref, m_origin, buildLegacyExternalCaller(),
                            m_precompiledManager.get(), m_contextID, m_seq, m_hashImpl))
                    {
                        HOST_CONTEXT_LOG(DEBUG) << "Auth check failed";
                        evmResult = std::move(*result);
                    }
                }
            }

            if (!evmResult)
            {
                // EIP-7623 (Prague+): debit normal calldata (4/16) upfront; floor applied at
                // finalize in TransactionExecutorImpl. 21000 base via consumeTransferGas.
                if (m_level == 0 && m_web3Tx && m_rev.eip7623)
                {
                    auto& msg = mutableMessage();
                    auto const components = executor::calcEip7623Components(
                        bcos::bytesConstRef(msg.input_data, msg.input_size));
                    const int64_t normalCalldata = components.normalCost;
                    if (msg.gas < normalCalldata)
                    {
                        evmResult.emplace(
                            makeErrorEVMCResult(m_hashImpl, protocol::TransactionStatus::OutOfGas,
                                EVMC_OUT_OF_GAS, fixErrorHandling ? 0 : msg.gas,
                                "EIP-7623 calldata OOG", fixErrorHandling));
                    }
                    else
                    {
                        msg.gas -= normalCalldata;
                    }
                }

                // Transfer first, then proceed execute
                if (m_rev.enable_balance_transfer &&
                    !::ranges::equal(ref->value.bytes, executor::EMPTY_EVM_BYTES32.bytes))
                {
                    bool shouldTransfer =
                        m_rev.fix_delegatecall_transfer ?
                            ((ref->kind == EVMC_CALL && (ref->flags & EVMC_STATIC) == 0) ||
                                (ref->kind == EVMC_CREATE) || ref->kind == EVMC_CREATE2) :
                            true;
                    if (shouldTransfer)
                    {
                        co_await transferBalance(*ref);
                    }
                }

                if (ref->kind == EVMC_CREATE || ref->kind == EVMC_CREATE2)
                {
                    evmResult.emplace(co_await executeCreate());
                }
                else
                {
                    evmResult.emplace(co_await executeCall());
                }
            }
        }
        catch (protocol::OutOfGas& e)
        {
            HOST_CONTEXT_LOG(DEBUG) << "OutOfGas exception: " << boost::diagnostic_information(e);
            // FIB-89: use 0 instead of potentially uninitialized evmResult->gas_left
            evmResult.emplace(makeErrorEVMCResult(m_hashImpl, protocol::TransactionStatus::OutOfGas,
                EVMC_OUT_OF_GAS, 0, e.what(), fixErrorHandling));
        }
        catch (protocol::NotEnoughCashError& e)
        {
            HOST_CONTEXT_LOG(DEBUG)
                << "NotEnoughCash exception: " << boost::diagnostic_information(e);
            // FIB-88: fatal error consumes all gas when bugfix flag enabled
            evmResult.emplace(
                makeErrorEVMCResult(m_hashImpl, protocol::TransactionStatus::NotEnoughCash,
                    EVMC_INSUFFICIENT_BALANCE, fixErrorHandling ? 0 : ref->gas, e.what()));
        }
        catch (NotFoundCodeError& e)
        {
            HOST_CONTEXT_LOG(DEBUG)
                << "Not found code exception: " << boost::diagnostic_information(e);
            // STATIC_CALL or DELEGATE_CALL, the EVMC_SUCCESS is returned when the contract does
            // not exist
            using namespace std::string_literals;
            if (ref->flags == EVMC_STATIC || ref->kind == EVMC_DELEGATECALL)
            {
                evmResult.emplace(makeErrorEVMCResult(
                    m_hashImpl, protocol::TransactionStatus::None, EVMC_SUCCESS, ref->gas, {}));
            }
            else
            {
                // FIB-88: EVMC_REVERT preserves ref->gas per EVM spec
                evmResult.emplace(
                    makeErrorEVMCResult(m_hashImpl, protocol::TransactionStatus::RevertInstruction,
                        EVMC_REVERT, ref->gas, "Call address error."s, fixErrorHandling));
            }
        }
        catch (std::exception& e)
        {
            HOST_CONTEXT_LOG(DEBUG) << "Execute exception: " << boost::diagnostic_information(e);
            // FIB-88: fatal error consumes all gas
            // FIB-92: use Unknown instead of OutOfGas for EVMC_INTERNAL_ERROR
            evmResult.emplace(makeErrorEVMCResult(m_hashImpl,
                fixErrorHandling ? protocol::TransactionStatus::Unknown :
                                   protocol::TransactionStatus::OutOfGas,
                EVMC_INTERNAL_ERROR, fixErrorHandling ? 0 : ref->gas, "", fixErrorHandling));
        }

        if (evmResult->gas_left < 0)
        {
            HOST_CONTEXT_LOG(DEBUG) << "Execute gas < 0: " << evmResult->gas_left;
            // FIB-88: fatal error consumes all gas when bugfix flag enabled
            evmResult.emplace(makeErrorEVMCResult(m_hashImpl, protocol::TransactionStatus::OutOfGas,
                EVMC_OUT_OF_GAS, fixErrorHandling ? 0 : ref->gas, "", fixErrorHandling));
        }

        if (evmResult->status_code != EVMC_SUCCESS)
        {
            co_await m_rollbackableStorage.get().rollback(savepoint);
            co_await m_rollbackableTransientStorage.get().rollback(transientSavepoint);
            m_logs.clear();
        }

        HOST_CONTEXT_LOG(TRACE) << "HostContext execute finished, kind: " << ref->kind
                                << " level: " << m_level << " seq: " << m_seq << " " << *evmResult;
        if (topCheckpointGuard && evmResult->status_code == EVMC_SUCCESS)
        {
            topCheckpointGuard->commit();
        }
        co_return std::move(*evmResult);
    }

    task::Task<EVMCResult> externalCall(const evmc_message& message, auto&&... /*unused*/)
    {
        ++m_seq;
        HOST_CONTEXT_LOG(TRACE) << "External call, seq: " << m_seq;

        auto senderAccount = getAccount(*this, message.sender);
        auto nonceStr = co_await senderAccount.nonce();
        auto nonce = u256(nonceStr.value_or(std::string("0")));

        std::optional<bcos::executor::Eip2929CheckpointGuard> checkpointGuard;
        if (m_eip2929Access && executor::eip2929Enabled(m_rev))
        {
            checkpointGuard.emplace(m_eip2929Access);
            if (message.kind == EVMC_CREATE || message.kind == EVMC_CREATE2)
            {
                // EVM CREATE passes empty code_address; pin must match Policy::deriveMessage
                // resolution.
                auto const resolved = Policy::deriveMessage(m_web3Tx, message,
                    m_blockHeader.get().number(), m_contextID, m_seq, nonce, m_hashImpl);
                m_eip2929Access->setCreateRollbackPin(resolved.code_address);
            }
        }

        HostContext hostcontext(innerConstructor, m_rollbackableStorage.get(),
            m_rollbackableTransientStorage.get(), m_blockHeader, message, m_origin, {}, m_contextID,
            m_seq, m_precompiledManager.get(), m_rev, *m_policy, m_hashImpl, m_web3Tx, nonce,
            m_eip2930AccessList, m_web3TypedTxKindForAccessList, m_eip2929Access);

        co_await hostcontext.prepare();
        auto result = co_await hostcontext.execute();
        auto& logs = hostcontext.logs();
        if (result.status_code == EVMC_SUCCESS && !logs.empty())
        {
            m_logs.reserve(m_logs.size() + ::ranges::size(logs));
            ::ranges::move(logs, std::back_inserter(m_logs));
        }
        if (checkpointGuard)
        {
            if (result.status_code == EVMC_SUCCESS)
            {
                checkpointGuard->commit();
            }
        }
        co_return result;
    }

    std::vector<protocol::LogEntry>& logs() & { return m_logs; }

    evmc_access_status accessAccount(const evmc_address& addr) noexcept
    {
        if (!m_eip2929Access || !executor::eip2929Enabled(m_rev))
        {
            return EVMC_ACCESS_COLD;
        }
        return m_eip2929Access->warmUpAddress(addr) ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
    }

    evmc_access_status accessStorage(const evmc_address& addr, const evmc_bytes32& key) noexcept
    {
        if (!m_eip2929Access || !executor::eip2929Enabled(m_rev))
        {
            return EVMC_ACCESS_COLD;
        }
        return m_eip2929Access->warmUpStorage(addr, key) ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
    }

public:
    // ── evmc::HostInterface ──
    // const_cast note: coroutines modify internal state machine (promise/awaiter),
    // not EVM-observable state (storage/balance/code). Same pattern as evmone::MockedHost.

    bool account_exists(const address& addr) const noexcept final
    {
        return syncWait(const_cast<HostContext*>(this)->exists(addr));
    }

    bytes32 get_storage(const address& addr, const bytes32& key) const noexcept final
    {
        return syncWait(const_cast<HostContext*>(this)->get(&key));
    }

    evmc_storage_status set_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept final
    {
        auto status = sstoreStatus(&key, &value);
        syncWait(this->set(&key, &value));
        return status;
    }

    uint256be get_balance(const address& addr) const noexcept final
    {
        return toEvmC(syncWait(const_cast<HostContext*>(this)->balance(addr)));
    }

    size_t get_code_size(const address& addr) const noexcept final
    {
        return syncWait(const_cast<HostContext*>(this)->codeSizeAt(addr));
    }

    bytes32 get_code_hash(const address& addr) const noexcept final
    {
        return toEvmC(syncWait(const_cast<HostContext*>(this)->codeHashAt(addr)));
    }

    size_t copy_code(const address& addr, size_t code_offset, uint8_t* buffer_data,
        size_t buffer_size) const noexcept final
    {
        auto* self = const_cast<HostContext*>(this);
        auto entry = syncWait(self->code(addr));
        if (!entry || code_offset >= static_cast<size_t>(entry->size()))
            return 0;
        auto code = entry->get();
        size_t n = std::min(code.size() - code_offset, buffer_size);
        std::copy_n(&code[code_offset], n, buffer_data);
        return n;
    }

    bool selfdestruct(const address& addr, const address& beneficiary) noexcept final
    {
        this->suicide();
        return m_policy->selfdestruct(addr, beneficiary);
    }

    Result call(const evmc_message& msg) noexcept final
    {
        auto result = syncWait(externalCall(msg));
        evmc_result raw = result;
        result.release = nullptr;
        return Result(raw);
    }

    evmc_tx_context get_tx_context() const noexcept final
    {
        return {
            .tx_gas_price = toEvmC(gasPrice()),
            .tx_origin = origin(),
            .block_coinbase = {},
            .block_number = blockNumber(),
            .block_timestamp = m_policy->convertTimestamp(m_blockHeader.get().timestamp()),
            .block_gas_limit = blockGasLimit(),
            .block_prev_randao = {},
            .chain_id = chainId(),
            .block_base_fee = {},
            .blob_base_fee = {},
            .blob_hashes = {},
            .blob_hashes_count = 0,
            .initcodes = {},
            .initcodes_count = 0,
        };
    }

    evmc_bytes32 get_block_hash(int64_t number) const noexcept final
    {
        return toEvmC(syncWait(const_cast<HostContext*>(this)->blockHash(number)));
    }

    void emit_log(const address& addr, const uint8_t* data, size_t data_size,
        const bytes32 topics[], size_t num_topics) noexcept final
    {
        h256s htopics;
        htopics.reserve(num_topics);
        for (size_t i = 0; i < num_topics; ++i)
            htopics.emplace_back(topics[i].bytes);
        this->log(addr, std::move(htopics), bytesConstRef{data, data_size});
    }

    evmc_access_status access_account(const address& addr) noexcept final
    {
        return this->accessAccount(addr);
    }

    evmc_access_status access_storage(const address& addr, const bytes32& key) noexcept final
    {
        return this->accessStorage(addr, key);
    }

    bytes32 get_transient_storage(const address& addr, const bytes32& key) const noexcept final
    {
        return syncWait(const_cast<HostContext*>(this)->getTransientStorage(&key));
    }

    void set_transient_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept final
    {
        syncWait(const_cast<HostContext*>(this)->setTransientStorage(&key, &value));
    }

private:
    // SSTORE status — migrated from EVMHostInterface.h
    evmc_storage_status sstoreStatus(const bytes32* key, const bytes32* value)
    {
        bool newIsZero =
            concepts::bytebuffer::equalTo(value->bytes, executor::EMPTY_EVM_BYTES32.bytes);

        evmc_storage_status status;
        if (m_rev.fix_storage_status)
        {
            auto existingValue =
                syncWait(const_cast<HostContext*>(this)->get(key, storage2::DIRECT));
            const bool existingIsZero = concepts::bytebuffer::equalTo(
                existingValue.bytes, executor::EMPTY_EVM_BYTES32.bytes);
            if (newIsZero)
                status = existingIsZero ? EVMC_STORAGE_ASSIGNED : EVMC_STORAGE_DELETED;
            else
                status = existingIsZero ? EVMC_STORAGE_ADDED : EVMC_STORAGE_MODIFIED;
        }
        else
        {
            status = newIsZero ? EVMC_STORAGE_DELETED : EVMC_STORAGE_MODIFIED;
        }
        return status;
    }

private:
    void warmEip2929AtTransactionEntry() noexcept
    {
        auto const& ref = message();
        if (!executor::eip2929TransactionEntryWarmEnabled(m_level, m_rev, m_eip2929Access.get()))
        {
            return;
        }

        executor::Eip2929TxPrewarmInput input;
        input.revision = m_revision;
        input.origin = m_origin;
        if (ref.kind != EVMC_CREATE && ref.kind != EVMC_CREATE2)
        {
            input.callee = ref.recipient;
        }
        if (ref.kind == EVMC_CREATE || ref.kind == EVMC_CREATE2)
        {
            input.createCodeAddress = ref.code_address;
        }
        // TODO(EIP-3651): set input.coinbase from block sealer at revision >= EVMC_SHANGHAI.
        input.web3TypedTxKind = m_web3TypedTxKindForAccessList;
        input.accessList = m_eip2930AccessList.get();
        executor::warmEip2929AtTransactionEntry(
            *m_eip2929Access, input, [](bcos::Address const& addr) { return toEvmC(addr); });
    }

    void prepareCreate()
    {
        auto& ref = message();
        bytesConstRef createCode(ref.input_data, ref.input_size);
        m_executable = std::make_shared<Executable>(createCode);
    }

    task::Task<EVMCResult> executeCreate()
    {
        auto& ref = mutableMessage();
        consumeTransferGas(ref);
        captureGasSettlementSnapshotBeforeEvm();

        if (m_blockHeader.get().number() != 0)
        {
            std::string authTablePath;
            // FIB-82: when feature_raw_address is on, m_recipientAccount.path() returns a binary
            // path, but ContractAuthMgrPrecompiled always looks up auth tables using hex paths.
            // Force hex to match the lookup path.
            if (m_rev.fix_auth_check && m_rev.use_raw_address)
            {
                authTablePath =
                    std::string(executor::USER_APPS_PREFIX) + address2HexString(ref.code_address);
            }
            else
            {
                authTablePath = std::string(co_await m_recipientAccount.path());
            }
            co_await createAuthTable(m_rollbackableStorage.get(), m_blockHeader, ref, m_origin,
                authTablePath, buildLegacyExternalCaller(), m_precompiledManager.get(), m_contextID,
                m_seq, m_ledgerConfig);
        }

        if (m_web3Tx && m_level != 0)
        {
            auto senderAccount = getAccount(*this, ref.sender);
            co_await senderAccount.increaseNonce();
        }

        co_await m_recipientAccount.create();
        auto bugfixNest = m_rev.fix_nonce_init;
        if (bugfixNest)
        {
            co_await m_recipientAccount.setNonce("1");
        }
        auto result = m_executable->m_vmInstance.execute(
            interface, this, m_revision, std::addressof(ref), ref.input_data, ref.input_size);
        if (result.status_code == EVMC_SUCCESS)
        {
            auto code = bytesConstRef(result.output_data, result.output_size);
            auto codeHash = m_hashImpl.get().hash(code);
            co_await m_recipientAccount.setCode(code.toBytes(), std::string(m_abi), codeHash);
            if (!bugfixNest)
            {
                co_await m_recipientAccount.setNonce("1");
            }
            result.gas_left -= result.output_size * bcos::executor::VMSchedule().createDataGas;
            result.create_address = ref.code_address;

            // Clear the output
            if (result.release)
            {
                result.release(std::addressof(result));
                result.release = nullptr;
            }
            result.output_data = nullptr;
            result.output_size = 0;
        }

        co_return result;
    }

    void consumeTransferGas(evmc_message& ref)
    {
        if (m_level == 0)
        {
            if (ref.gas < executor::BALANCE_TRANSFER_GAS)
            {
                BOOST_THROW_EXCEPTION(protocol::OutOfGas{});
            }
            ref.gas -= executor::BALANCE_TRANSFER_GAS;
        }
    }

    void processDynamicPrecompiled()
    {
        if (hasPrecompiledPrefix(m_executable->m_code->get()))
        {
            auto& message = mutableMessage();
            auto code = m_executable->m_code->get();

            std::vector<std::string> codeParameters{};
            boost::split(codeParameters, code, boost::is_any_of(","));
            if (codeParameters.size() < 3)
            {
                BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "CallDynamicPrecompiled error code field."));
            }
            message.code_address = message.recipient;
            // precompiled的地址，是不是写到code_address里更合理？考虑delegate call
            // Is it more reasonable to write the address of precompiled in the code_address?
            // Consider Delegate Call
            message.recipient = unhexAddress(codeParameters[1]);
            codeParameters.erase(codeParameters.begin(), codeParameters.begin() + 2);

            codec::abi::ContractABICodec codec(m_hashImpl);
            m_dynamicPrecompiledInput = codec.abiIn(
                "", codeParameters, bcos::bytesConstRef(message.input_data, message.input_size));

            message.input_data = m_dynamicPrecompiledInput.data();
            message.input_size = m_dynamicPrecompiledInput.size();

            HOST_CONTEXT_LOG(TRACE)
                << LOG_DESC("callDynamicPrecompiled") << LOG_KV("codeAddr", message.code_address)
                << LOG_KV("recvAddr", message.recipient) << LOG_KV("code", code);

            if (m_preparedPrecompiled = m_precompiledManager.get().getPrecompiled(
                    message.recipient, m_rev, m_policy->features());
                m_preparedPrecompiled == nullptr)
            {
                BOOST_THROW_EXCEPTION(NotFoundCodeError());
            }
        }
    }

    task::Task<void> prepareCall()
    {
        auto& ref = message();
        // delegatecall static precompiled is not allowed
        if (m_policy->allowDelegateCallToPrecompile() || ref.kind != EVMC_DELEGATECALL)
        {
            m_preparedPrecompiled = m_precompiledManager.get().getPrecompiled(
                ref.code_address, m_rev, m_policy->features());
        }
    }

    task::Task<EVMCResult> executeCall()
    {
        auto& ref = mutableMessage();
        // 先扣除BALANCE_TRANSFER_GAS
        // First deduct the BALANCE_TRANSFER_GAS.
        consumeTransferGas(ref);
        captureGasSettlementSnapshotBeforeEvm();

        if (m_preparedPrecompiled != nullptr)
        {
            co_return executor_v1::callPrecompiled(*m_preparedPrecompiled,
                m_rollbackableStorage.get(), m_blockHeader, ref, m_origin,
                buildLegacyExternalCaller(), m_precompiledManager.get(), m_contextID, m_seq,
                m_ledgerConfig.get().authCheckStatus(), m_rev, m_revision,
                m_rev.fix_error_handling);
        }

        if (m_executable = co_await getExecutable(
                m_rollbackableStorage.get(), ref.code_address, m_revision, m_rev.use_raw_address);
            !m_executable)
        {
            if (ref.input_size > 0)
            {
                BOOST_THROW_EXCEPTION(NotFoundCodeError());
            }

            co_return EVMCResult{evmc_result{.status_code = EVMC_SUCCESS,
                                     .gas_left = ref.gas,
                                     .gas_refund = 0,
                                     .output_data = nullptr,
                                     .output_size = 0,
                                     .release = nullptr,
                                     .create_address = {},
                                     .padding = {}},
                protocol::TransactionStatus::None};
        }
        processDynamicPrecompiled();

        if (m_preparedPrecompiled != nullptr)
        {
            co_return executor_v1::callPrecompiled(*m_preparedPrecompiled,
                m_rollbackableStorage.get(), m_blockHeader, ref, m_origin,
                buildLegacyExternalCaller(), m_precompiledManager.get(), m_contextID, m_seq,
                m_ledgerConfig.get().authCheckStatus(), m_rev, m_revision,
                m_rev.fix_error_handling);
        }

        co_return m_executable->m_vmInstance.execute(interface, this, m_revision,
            std::addressof(ref), (const uint8_t*)m_executable->m_code->data(),
            m_executable->m_code->size());
    }
};

}  // namespace bcos::executor_v1::hostcontext
