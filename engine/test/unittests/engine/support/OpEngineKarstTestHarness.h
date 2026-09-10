/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file OpEngineKarstTestHarness.h
 * @brief Shared OP Engine FCU/getPayload fixtures extracted from OpEngineServiceParityTest.
 */
#pragma once

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/OpEngineService.inl"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/engine/OpTime.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Exceptions.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/tests/OpSchedulerSeamTestHelpers.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <functional>
#include <latch>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

namespace op_engine_parity_test
{

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

struct StubMemPool
{
    std::vector<bcos::crypto::HashType> removed;
    std::vector<bcos::protocol::Transaction::Ptr> pool;
    void removeByHash(std::span<bcos::crypto::HashType const> hashes)
    {
        removed.insert(removed.end(), hashes.begin(), hashes.end());
    }
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t limit, View&, OutputIt out)
    {
        auto const n = std::min<int64_t>(limit, static_cast<int64_t>(pool.size()));
        for (int64_t i = 0; i < n; ++i)
        {
            *out++ = pool[static_cast<std::size_t>(i)];
        }
    }
};

/// TEST DOUBLE — NOT the real import/commitment path. executeBlock/importExecute return
/// a header with FABRICATED roots (stateRoot/receiptsRoot zero, txsRoot from a knob), so
/// the engine's commitment gate is only exercised against invented values. First
/// executeBlock fails with a structured culprit; later calls succeed so the build retry
/// loop can finish (BH capacity / BC evict). Any test that needs the real import path
/// (execution on the parent plane, the commitment gate, BLOCKHASH seeds) must use the
/// real OpScheduler instead — see OpEngineImportFcuTest (real delegate) and
/// OpNewPayloadRpcE2eTest (real delegate, end to end). Do not cite a suite built on
/// this stub as import-path coverage.
struct FabricatedRootsStub : bcos::scheduler::SchedulerInterface
{
    bcos::h256 culprit;
    bool rejectAsCapacity = false;
    bool failFirst = true;
    bool failCommit = false;
    // Error code for the stub commit failure. -1 is the generic stub; the mapDelegateError
    // routing test sets real SchedulerError codes (UnknownError = the dropped-pending shape
    // a concurrent reset produces in OpScheduler; OpConsensusRejected = the one code the
    // service is allowed to answer INVALID for).
    int commitErrorCode = -1;
    bool failReset = false;
    int executeCalls = 0;
    int commitCalls = 0;
    /// TxsRoot stamped onto importExecute-produced headers (import payloads with no
    /// transactions carry the empty-list root; default zero matches legacy stubs).
    bcos::h256 txsRootToReturn{};
    bcos::h256 executedWithdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
    bcos::protocol::BlockHeaderFactory::Ptr headerFactory;

    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        ++executeCalls;
        if (failFirst && executeCalls == 1)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            if (rejectAsCapacity)
            {
                *error << bcos::engine::OpRejectIsCapacity(true);
            }
            callback(std::move(error), nullptr, false);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        header->setWithdrawalsRoot(executedWithdrawalsRoot);
        header->setBlobGasUsed(0);
        callback(nullptr, std::move(header), false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        ++commitCalls;
        if (failCommit)
        {
            callback(BCOS_ERROR_PTR(commitErrorCode, "stub commit failure"), nullptr);
            return;
        }
        callback(nullptr, nullptr);
    }
    /// S5 import arm: FABRICATED header (no execution happens here — see the struct's
    /// doc). The engine's commitment gate runs on whatever this returns, which is why
    /// this stub is NOT import-path coverage. No commit happens on the import path by
    /// design.
    void importExecute(bcos::protocol::Block::Ptr,
        std::vector<bcos::protocol::BlockHeader::Ptr> const&,
        std::shared_ptr<void> const& parentFlat,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr,
            std::shared_ptr<void>, std::shared_ptr<void>)>
            callback) override
    {
        ++executeCalls;
        if (failFirst && executeCalls == 1)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            callback(std::move(error), nullptr, nullptr, nullptr);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setTxsRoot(txsRootToReturn);
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        header->setWithdrawalsRoot(executedWithdrawalsRoot);
        header->setBlobGasUsed(0);
        callback(nullptr, std::move(header), nullptr, nullptr);
    }
    void status(std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)>) override
    {}
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)>) override
    {}
    void reset(std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(failReset ? BCOS_ERROR_PTR(-1, "stub reset failure") : nullptr);
    }
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)>) override {}
    bcos::task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)>) override
    {}
};

/// Rejects `culprit` whenever that hash is in the block; if only `successor` remains,
/// rejects it as a non-capacity nonce-gap (the R3-F1 production shape).
/// `culprit`/`successor` are pool hashes (OpCulpritTxHash). `*EnvHash` are
/// keccak(reassembled envelope), matching buildOpBlock's transactionHash.
struct NonceChainScheduler : FabricatedRootsStub
{
    bcos::h256 successor;
    bcos::h256 culpritEnvHash;
    bcos::h256 successorEnvHash;

    void executeBlock(bcos::protocol::Block::Ptr block, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        ++executeCalls;
        bool hasCulprit = false;
        bool hasSuccessor = false;
        if (block)
        {
            for (auto txView : block->transactions())
            {
                auto tx = std::move(txView).toShared();
                if (!tx)
                {
                    continue;
                }
                auto const hash = tx->hash();
                if (hash == culpritEnvHash)
                {
                    hasCulprit = true;
                }
                if (hash == successorEnvHash)
                {
                    hasSuccessor = true;
                }
            }
        }
        if (hasCulprit)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            if (rejectAsCapacity)
            {
                *error << bcos::engine::OpRejectIsCapacity(true);
            }
            callback(std::move(error), nullptr, false);
            return;
        }
        if (hasSuccessor)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: nonce gap");
            *error << bcos::engine::OpCulpritTxHash(successor);
            callback(std::move(error), nullptr, false);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        header->setWithdrawalsRoot(executedWithdrawalsRoot);
        header->setBlobGasUsed(0);
        callback(nullptr, std::move(header), false);
    }
};

class TestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

/// EIP-1559 envelope that opEnvelopeToTars can decode, plus the signing payload +
/// 65-byte signature that reassembleWeb3RawTransaction expects on the seal path.
struct DecodableWeb3Tx
{
    bcos::protocol::Transaction::Ptr tx;
    std::string rawHex;
};

inline bcos::h256 envelopeHashOf(bcos::protocol::Transaction::Ptr const& tx)
{
    bcos::crypto::Keccak256 hasher;
    auto const raw = bcostars::protocol::reassembleWeb3RawTransaction(
        tx->extraTransactionBytes(), tx->signatureData());
    return hasher.hash(bcos::ref(raw));
}

inline DecodableWeb3Tx makeDecodableWeb3Tx(
    uint64_t nonce, bcos::crypto::KeyPairInterface* keyPair = nullptr, bcos::bytes data = {})
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 1;
    w3.nonce = nonce;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 1;
    w3.gasLimit = 21000;
    w3.to = bcos::Address("abcdabcdabcdabcdabcdabcdabcdabcdabcdabcd");
    w3.value = 0;
    w3.data = std::move(data);
    bcos::crypto::Secp256k1Crypto secp;
    auto owned = keyPair == nullptr ? secp.generateKeyPair() : nullptr;
    auto const& kp = keyPair != nullptr ? *keyPair : *owned;
    auto const sig = secp.sign(kp, w3.hashForSign(), false);
    BOOST_REQUIRE(sig);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];

    auto const raw = w3.encode();
    auto const signPayload = w3.encodeForSign();
    {
        bcos::rpc::Web3Transaction decoded;
        bcos::bytes copy = raw;
        bcos::bytesRef ref{copy.data(), copy.size()};
        auto err = bcos::codec::rlp::decode(ref, decoded);
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE(ref.empty());
        BOOST_REQUIRE(bcos::engine::engine_common::op::opEnvelopeToTars(raw, bcos::h256{}));
    }
    bcos::bytes signature(65, 0);
    std::copy(w3.signatureR.begin(), w3.signatureR.end(), signature.begin());
    std::copy(w3.signatureS.begin(), w3.signatureS.end(), signature.begin() + 32);
    signature[64] = static_cast<bcos::byte>(w3.signatureV);
    bcos::bytes reassembled;
    {
        reassembled = bcostars::protocol::reassembleWeb3RawTransaction(
            bcos::bytesConstRef(signPayload.data(), signPayload.size()),
            bcos::bytesConstRef(signature.data(), signature.size()));
        BOOST_REQUIRE(bcos::engine::engine_common::op::opEnvelopeToTars(reassembled, bcos::h256{}));
    }

    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().extraTransactionBytes.assign(signPayload.begin(), signPayload.end());
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    tx->setNonce("0x" + std::to_string(nonce));
    tx->forceSender(bcos::fromHex(w3.sender()));
    bcos::crypto::Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(static_cast<int64_t>(nonce));
    // The build loop's culprit matching rests on this identity: the producer tags the
    // culprit with keccak256(signed envelope) (OpBlockExecute) and the consumer matches it
    // against the sealed carrier's hash() (OpEngineService.inl) — they must be the same
    // bytes, or every reject (capacity or not) falls through to -32603.
    BOOST_REQUIRE_EQUAL(
        bcos::crypto::keccak256Hash(bcos::ref(reassembled)).hex(), tx->hash().hex());
    return DecodableWeb3Tx{.tx = std::move(tx), .rawHex = bcos::toHexStringWithPrefix(raw)};
}

struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };
    template <class Storage>
    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }
    template <class Storage>
    bcos::task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

using EngineOpSchedulerBase = bcos::evm::engine::OpSchedulerSeam<ViewType>;
/// Production seam synthesizes from L1BlockInfo. Fixtures keep the zero envelope.
struct EngineOpScheduler : EngineOpSchedulerBase
{
    using EngineOpSchedulerBase::EngineOpSchedulerBase;
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope(uint64_t timestampSeconds) const
    {
        return bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(
            configAt(timestampSeconds).has_da_footprint);
    }
};
using EthLegacyEngine =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;
using OpEngine = bcos::engine::OpEngineService<StubMemPool, MLS, EngineOpScheduler>;

static_assert(bcos::engine::EngineServiceConcept<EthLegacyEngine>);
static_assert(bcos::engine::EngineServiceConcept<OpEngine>);

constexpr bcos::protocol::BlockNumber c_headOrderingBlockNumber = 40;
constexpr bcos::protocol::BlockNumber c_safeOrderingBlockNumber = 41;
constexpr bcos::protocol::BlockNumber c_finalizedOrderingBlockNumber = 42;

constexpr char const* c_opNewPayloadVersionMismatchMessage =
    "newPayload version does not match the OP Engine API profile at payload timestamp";
constexpr char const* c_safeAboveHeadMessage =
    "Forkchoice safe block number must not exceed head block number";
constexpr char const* c_finalizedAboveHeadMessage =
    "Forkchoice finalized block number must not exceed head block number";
constexpr char const* c_finalizedAboveSafeMessage =
    "Forkchoice finalized block number must not exceed safe block number";

inline bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

inline bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

inline void registerVerifiedBlock(
    MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::storage::Entry hashEntry;
    hashEntry.set(blockHash.asBytes());
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(hashEntry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

inline void registerHashToNumberOnly(
    MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

inline void registerCurrentBlockNumber(MLS& multiLayerStorage, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

inline void registerParentHeader(MLS& multiLayerStorage, bcos::protocol::BlockFactory& blockFactory,
    int64_t number, int64_t timestampMs)
{
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setTimestamp(timestampMs);
    header->setGasLimit(30'000'000);
    header->setGasUsed(0);
    header->setExtraData(bcos::fromHex("00000000fa00000006"));
    header->setBaseFee(bcos::u256(1'000'000'000));
    header->setBlobGasUsed(0);
    bcos::bytes encoded;
    header->encode(encoded);
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(std::move(encoded));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(number)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

inline bcos::engine::NewPayloadRequest makeValidIsthmusNewPayload(
    bcos::protocol::BlockFactory& blockFactory, bcos::h256 const& parentHash,
    bcos::protocol::BlockNumber blockNumber)
{
    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    auto& payload = request.executionPayload;
    payload.parentHash = parentHash;
    payload.blockNumber = blockNumber;
    payload.timestamp = 1'700'000'000'000ULL;
    payload.gasLimit = 30'000'000;
    payload.gasUsed = 0;
    payload.baseFeePerGas = 1;
    payload.transactions = {};
    payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    payload.withdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
    payload.excessBlobGas = bcos::u256(0);
    payload.blobGasUsed = bcos::u256(0);
    payload.extraData = bcos::fromHex("00000000fa00000006");
    request.parentBeaconBlockRoot = bcos::h256{};
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
    auto header =
        bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory.blockHeaderFactory(),
            payload, txRoot, *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    return request;
}

/// Field-wise pin of the strict-compared ExecutionPayload set (the 16 fields
/// compareWithBuiltPayload enforces plus the tx list) between two struct copies —
/// used by the wire round trip and the version-window tests so a dropped field
/// cannot pass silently.
inline void checkSameExecutionPayload(
    bcos::engine::ExecutionPayload const& left, bcos::engine::ExecutionPayload const& right)
{
    BOOST_CHECK_EQUAL(left.parentHash.hex(), right.parentHash.hex());
    BOOST_CHECK_EQUAL(left.feeRecipient.hex(), right.feeRecipient.hex());
    BOOST_CHECK_EQUAL(left.stateRoot.hex(), right.stateRoot.hex());
    BOOST_CHECK_EQUAL(left.receiptsRoot.hex(), right.receiptsRoot.hex());
    BOOST_CHECK_EQUAL(bcos::toHex(bcos::bytes(left.logsBloom.begin(), left.logsBloom.end())),
        bcos::toHex(bcos::bytes(right.logsBloom.begin(), right.logsBloom.end())));
    BOOST_CHECK_EQUAL(left.prevRandao.hex(), right.prevRandao.hex());
    BOOST_CHECK(left.blockNumber == right.blockNumber);
    BOOST_CHECK(left.gasLimit == right.gasLimit);
    BOOST_CHECK(left.gasUsed == right.gasUsed);
    BOOST_CHECK(left.timestamp == right.timestamp);
    BOOST_CHECK_EQUAL(bcos::toHex(left.extraData), bcos::toHex(right.extraData));
    BOOST_CHECK(left.baseFeePerGas == right.baseFeePerGas);
    BOOST_CHECK_EQUAL(left.blockHash.hex(), right.blockHash.hex());
    BOOST_REQUIRE_EQUAL(left.withdrawals.has_value(), right.withdrawals.has_value());
    BOOST_REQUIRE_EQUAL(left.withdrawalsRoot.has_value(), right.withdrawalsRoot.has_value());
    if (left.withdrawalsRoot.has_value())
        BOOST_CHECK_EQUAL(left.withdrawalsRoot->hex(), right.withdrawalsRoot->hex());
    BOOST_REQUIRE_EQUAL(left.blobGasUsed.has_value(), right.blobGasUsed.has_value());
    if (left.blobGasUsed.has_value())
        BOOST_CHECK(*left.blobGasUsed == *right.blobGasUsed);
    BOOST_REQUIRE_EQUAL(left.excessBlobGas.has_value(), right.excessBlobGas.has_value());
    if (left.excessBlobGas.has_value())
        BOOST_CHECK(*left.excessBlobGas == *right.excessBlobGas);
    BOOST_REQUIRE_EQUAL(left.transactions.size(), right.transactions.size());
    for (std::size_t i = 0; i < left.transactions.size(); ++i)
        BOOST_CHECK_EQUAL(
            bcos::toHex(left.transactions[i].raw), bcos::toHex(right.transactions[i].raw));
}

template <typename Exception>
void checkBothExceptionMessages(auto&& leftAction, auto&& rightAction, char const* expectedMessage)
{
    BOOST_CHECK_EXCEPTION(leftAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
    BOOST_CHECK_EXCEPTION(rightAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
}

inline bcos::engine::PayloadAttributes makeOpPayloadAttributes()
{
    bcos::engine::PayloadAttributes attrs;
    // Whole-second milliseconds for Eth RLP timestamp validation.
    attrs.timestamp = 1'700'000'000'000ULL;
    attrs.prevRandao = bcos::h256(std::string(64, '2'));
    attrs.suggestedFeeRecipient = bcos::Address(std::string(40, '3'));
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = bcos::h256(std::string(64, '4'));
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes(8, 0);
    attrs.minBaseFee = 0;
    attrs.noTxPool = true;
    return attrs;
}

struct OpServicePair
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler scheduler{std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                    bcos::evm::opstack::OpForkSchedule::legacy(false)),
        {}};
    bcos::scheduler::SchedulerInterface::Ptr delegate;
    OpEngine service;

    explicit OpServicePair(bool allowSynthesizedL1Attributes = false,
        bcos::scheduler::SchedulerInterface::Ptr delegateIn = nullptr,
        std::shared_ptr<bcos::engine::DACaps> daCapsIn = nullptr,
        std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> scheduleIn = nullptr)
      : scheduler(scheduleIn ? std::move(scheduleIn) :
                               std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                   bcos::evm::opstack::OpForkSchedule::legacy(false)),
            {}),
        delegate(std::move(delegateIn)),
        service(memPool, storage, scheduler, blockFactory, bcos::engine::c_defaultBlockTxCountLimit,
            delegate, std::move(daCapsIn), allowSynthesizedL1Attributes)
    {}
};

/// Production parse: Jovian at 0s, Karst at 1000s.
inline std::shared_ptr<bcos::evm::opstack::OpForkSchedule> makeKarstProfileSchedule()
{
    using bcos::evm::opstack::OpForkSchedule;
    return std::make_shared<OpForkSchedule>(OpForkSchedule::parse("0:jovian,1000:karst"));
}

/// PayloadAttributes.timestamp is internal milliseconds (unix seconds × 1000).
inline constexpr std::uint64_t c_jovianPayloadTimestampMs = 999'000;
inline constexpr std::uint64_t c_karstPayloadTimestampMs = 1'000'000;
static_assert(c_jovianPayloadTimestampMs / 1000 == 999);
static_assert(c_karstPayloadTimestampMs / 1000 == 1000);

inline bcos::h256 fixtureHeadHash()
{
    return bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

inline bcos::engine::PayloadAttributes makeOpPayloadAttributesAt(std::uint64_t timestampMs)
{
    auto attrs = makeOpPayloadAttributes();
    attrs.timestamp = timestampMs;
    return attrs;
}

/// Pre-Holocene attrs. makeOpPayloadAttributes() carries Holocene+ fields (beacon
/// root, 8-byte eip1559Params, minBaseFee); leaving them set makes a Regolith V1 or
/// Canyon V2 FCU come back Invalid without throwing, so a NO_THROW assertion would
/// pass for the wrong reason.
inline bcos::engine::PayloadAttributes makeRegolithAttrs(std::uint64_t timestampMs)
{
    auto attrs = makeOpPayloadAttributesAt(timestampMs);
    attrs.withdrawals.reset();
    attrs.parentBeaconBlockRoot.reset();
    attrs.eip1559Params.reset();
    attrs.minBaseFee.reset();
    return attrs;
}

inline bcos::engine::PayloadAttributes makeCanyonAttrs(std::uint64_t timestampMs)
{
    auto attrs = makeRegolithAttrs(timestampMs);
    attrs.withdrawals.emplace();
    return attrs;
}

inline bcos::engine::PayloadAttributes makeEcotoneAttrs(std::uint64_t timestampMs)
{
    auto attrs = makeCanyonAttrs(timestampMs);
    attrs.parentBeaconBlockRoot = bcos::h256(std::string(64, '4'));
    return attrs;
}

/// FCU V3 build keyed by attrs.timestamp (ms). Parent/head timestamp is seeded separately.
/// Forced txs are deposits-only so a Jovian/Karst activation window (parent pre-fork,
/// attrs on the new fork) stays VALID — user envelopes are FCU-INVALID there.
inline bcos::engine::PayloadID buildPayloadAt(
    OpServicePair& pair, std::uint64_t attrsTimestampMs, std::uint64_t parentTimestampMs)
{
    // Real L1-attributes deposit (not 0x7e00): buildOpBlock rejects undecodable
    // envelopes as FCU INVALID. Jovian+ schedule always has DA footprint.
    auto const deposit = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(true);
    auto attrs = makeOpPayloadAttributesAt(attrsTimestampMs);
    attrs.transactions = std::vector<std::string>{bcos::toHexStringWithPrefix(deposit)};
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(
        pair.storage, *pair.blockFactory, 0, static_cast<int64_t>(parentTimestampMs));
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId.has_value());
    return *built.payloadId;
}

struct KarstProfilePair
{
    std::shared_ptr<FabricatedRootsStub> delegate{std::make_shared<FabricatedRootsStub>()};
    OpServicePair pair;

    KarstProfilePair() : pair(false, delegate, nullptr, makeKarstProfileSchedule())
    {
        delegate->failFirst = false;
        delegate->headerFactory = pair.blockFactory->blockHeaderFactory();
    }
};

}  // namespace op_engine_parity_test
