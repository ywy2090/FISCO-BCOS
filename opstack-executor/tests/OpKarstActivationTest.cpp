// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Q5: Jovian+ activation blocks are deposits-only, driven by the timestamp schedule
// (not the 176-byte L1-attributes heuristic). Karst profiles use TestBypass only.

#include "support/KarstNutHelpers.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/engine/OpTime.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpDepositEncode.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
#include <boost/test/unit_test.hpp>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using bcos::evm::OpConsensusError;
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace engine = bcos::evm::engine;
namespace op = bcos::evm::opstack;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;

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

using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;

struct UnusedView
{
};

constexpr uint64_t kJovianTsSec = 100;
constexpr int64_t kJovianTsMs = static_cast<int64_t>(kJovianTsSec) * 1000;
constexpr uint64_t kParentTsSec = 99;

const bcos::bytes kDepositEnvelope{bcos::byte{0x7e}, bcos::byte{0x01}};
const bcos::bytes kTypedEnvelope{bcos::byte{0x02}, bcos::byte{0x01}};

op::DepositTx depositWithJovianAttrs()
{
    evmc::bytes data(op::JovianL1AttributesLen, uint8_t{0});
    std::memcpy(
        data.data(), op::JovianL1AttributesSelector.data(), op::JovianL1AttributesSelector.size());
    op::DepositTx dep{};
    dep.gas_limit = 1'000'000;
    dep.data = std::move(data);
    return dep;
}

std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeader(int64_t timestampMs)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(timestampMs);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setGasLimit(bcos::u256(30'000'000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    return h;
}

bool isActivationUserTxError(OpConsensusError const& e)
{
    auto const w = std::string_view{e.what()};
    return w.find("unexpected non-deposit") != std::string_view::npos ||
           w.find("UnexpectedNonDepositTxInForkActivationBlock") != std::string_view::npos;
}

void runPreBlock(op::OpForkConfig const& cfg, op::OpForkSchedule const& schedule,
    uint64_t parentTsSec, int64_t blockTsMs, std::vector<bcos::bytes> const& rawTxs,
    std::vector<op::DepositTx> const& deposits)
{
    MutableStorage storage;
    auto header = makeHeader(blockTsMs);
    bcos::executor_v1::opstack::OpstackExecutor executor{nullptr, nullptr, cfg};
    std::optional<engine::detail::RecentBlockHashes<MutableStorage>> hashes;
    std::optional<std::string> hashErr;
    std::optional<uint16_t> scalar;
    engine::preBlockOpSteps(storage, *header, cfg, rawTxs, deposits, executor, hashes, hashErr,
        scalar, &schedule, parentTsSec);
}

bcos::protocol::Transaction::Ptr envelopeToTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto const txHash = hashImpl->hash(env);
    bcostars::Transaction tars;
    tars.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tars.extraTransactionHash.assign(txHash.begin(), txHash.end());
    tars.extraTransactionBytes.assign(env.begin(), env.end());
    if (!env.empty())
        tars.web3TypedTxKind = static_cast<tars::Char>(env[0]);
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
}

/// Execute an activation-height block with no parent header row in storage.
bcos::Error::Ptr executeActivationWithoutParent(std::shared_ptr<op::OpForkSchedule> schedule)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend(backendStorage);
    MLS mls(checkpointBackend);
    auto crypto =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto hashImpl = crypto->hashImpl();
    auto headerFactory = std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(crypto);
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(crypto);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(crypto);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        crypto, headerFactory, txFactory, receiptFactory);
    auto io = std::make_shared<bcos::IOServicePool>(1);
    auto scheduler = std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory,
        hashImpl, /*chainId=*/0x2105, schedule, blockFactory, mls, /*ledger=*/nullptr, io);

    auto depEnv = op::encodeDepositEnvelope(depositWithJovianAttrs());
    auto header = makeHeader(kJovianTsMs);
    auto block = blockFactory->createBlock();
    block->setBlockHeader(header);
    block->appendTransaction(envelopeToTx(depEnv, hashImpl));
    block->appendTransaction(envelopeToTx(kTypedEnvelope, hashImpl));

    bcos::Error::Ptr err;
    bool called = false;
    scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr, bool) {
            called = true;
            err = std::move(e);
        });
    BOOST_REQUIRE(called);
    return err;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpKarstActivationSuite)

BOOST_AUTO_TEST_CASE(JovianActivationBlockRejectsUserTx)
{
    // 178B Jovian attrs: the 176-byte L1-attributes heuristic must not be what rejects.
    // Header is internal milliseconds; schedule activations are Unix seconds (A13).
    auto schedule = opstack_test::isthmusThenJovian(kJovianTsSec);
    BOOST_CHECK_EQUAL(
        bcos::engine::unixSecondsFromInternalMillis(static_cast<uint64_t>(kJovianTsMs)),
        kJovianTsSec);

    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_EXCEPTION(runPreBlock(op::jovianConfig(), *schedule, kParentTsSec, kJovianTsMs,
                              {kDepositEnvelope, kTypedEnvelope}, {dep, op::DepositTx{}}),
        OpConsensusError, isActivationUserTxError);
}

BOOST_AUTO_TEST_CASE(JovianActivationBlockAllowsDepositsOnly)
{
    auto schedule = opstack_test::isthmusThenJovian(kJovianTsSec);
    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_NO_THROW(runPreBlock(
        op::jovianConfig(), *schedule, kParentTsSec, kJovianTsMs, {kDepositEnvelope}, {dep}));
}

BOOST_AUTO_TEST_CASE(KarstActivationBlockRejectsUserTx)
{
    auto schedule = opstack_test::karstOnlySchedule(/*karstTs=*/kJovianTsSec);
    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_EXCEPTION(runPreBlock(op::karstConfig(), *schedule, kParentTsSec, kJovianTsMs,
                              {kDepositEnvelope, kTypedEnvelope}, {dep, op::DepositTx{}}),
        OpConsensusError, isActivationUserTxError);
}

BOOST_AUTO_TEST_CASE(ResolveEngineForkAtKarstSelectsGetPayloadV5)
{
    auto schedule = opstack_test::karstOnlySchedule(/*karstTs=*/100);
    engine::OpSchedulerSeam<UnusedView> seam(schedule, op::L1BlockInfo{});
    auto resolved = seam.resolveEngineForkAt(100);
    auto* ctx = std::get_if<bcos::engine::EngineForkContext>(&resolved);
    BOOST_REQUIRE(ctx);
    BOOST_CHECK(ctx->forkId == bcos::engine::OpForkId::Karst);
    BOOST_CHECK(ctx->api.getPayload == bcos::engine::ApiVersion::V5);
    BOOST_CHECK(ctx->api.forkchoiceUpdated == bcos::engine::ApiVersion::V3);
    BOOST_CHECK(ctx->api.newPayload == bcos::engine::ApiVersion::V4);
    auto jov = seam.resolveEngineForkAt(99);
    BOOST_CHECK(std::get<bcos::engine::EngineForkContext>(jov).api.getPayload ==
                bcos::engine::ApiVersion::V4);
}

BOOST_AUTO_TEST_CASE(ResolveEngineForkAtRejectsBelowBaseline)
{
    // TestBypass: nonzero baseline so baseline-1 is representable as uint64.
    auto schedule = std::make_shared<op::OpForkSchedule>(
        op::OpForkSchedule{{{op::OpFork::Jovian, 50}}, op::OpForkSchedule::TestBypass{}});
    engine::OpSchedulerSeam<UnusedView> seam(schedule, op::L1BlockInfo{});
    auto resolved = seam.resolveEngineForkAt(49);
    auto* err = std::get_if<bcos::engine::OpForkResolutionError>(&resolved);
    BOOST_REQUIRE(err);
    BOOST_CHECK(*err == bcos::engine::OpForkResolutionError::UnsupportedTimestamp);
}

BOOST_AUTO_TEST_CASE(JovianActivationWithoutParentHeaderFailsClosed)
{
    auto err = executeActivationWithoutParent(opstack_test::isthmusThenJovian(kJovianTsSec));
    BOOST_REQUIRE(err);
    BOOST_CHECK(
        err->errorCode() == static_cast<int>(bcos::scheduler::SchedulerError::OpStorageFault) ||
        err->errorCode() == static_cast<int>(bcos::scheduler::SchedulerError::OpConsensusRejected));
    auto const msg = err->errorMessage();
    BOOST_CHECK_MESSAGE(msg.find("parent") != std::string::npos,
        "missing parent must fail closed, not execute; got: " + msg);
}

BOOST_AUTO_TEST_SUITE_END()
