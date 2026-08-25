// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEngineBranchSmokeTest — OP-mode EngineServiceImpl instantiation plus fork-profile matrix
// (Jovian/Karst FCU V3, getPayload V4/V5, newPayload V4, FCU V4 -38005).
#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) & { std::abort(); }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
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
    void removeByHash(std::span<bcos::crypto::HashType const>) {}
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t, View&, OutputIt)
    {}
};

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

/// Minimal delegate: echoes the provisional header so buildOpPayload can finish without
/// OpScheduler.
class StubSchedulerDelegate : public bcos::scheduler::SchedulerInterface
{
public:
    void executeBlock(bcos::protocol::Block::Ptr block, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        callback(nullptr, block->blockHeader(), false);
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        callback(nullptr, nullptr);
    }

    void status(
        std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        callback(nullptr, nullptr);
    }

    void call(bcos::protocol::Transaction::Ptr tx,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> cb) override
    {
        cb(nullptr, nullptr);
    }

    void reset(std::function<void(bcos::Error::Ptr)> callback) override { callback(nullptr); }

    void getCode(
        std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        callback(nullptr, {});
    }

    void getABI(
        std::string_view, std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        callback(nullptr, {});
    }

    bcos::task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }

    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }
};

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using OpEngine = bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;

const bcos::h256 kHeadHash{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
const bcos::h256 kSafeHash{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
const bcos::h256 kFinalizedHash{"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
constexpr bcos::protocol::BlockNumber kHeadNumber = 5;

bcos::protocol::BlockFactory::Ptr blockFactory()
{
    static auto factory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    return factory;
}

void seedHeadBlockNumber(MLS& storage, bcos::h256 const& hash, bcos::protocol::BlockNumber number)
{
    auto view = storage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(hash)},
        std::move(entry)));
    bcos::task::syncWait(storage.mergeView(std::move(view)));
}

bcos::engine::ForkchoiceState makeForkchoiceState()
{
    return bcos::engine::ForkchoiceState{kHeadHash, kSafeHash, kFinalizedHash};
}

bcos::engine::PayloadAttributes makeOpAttrs(uint64_t timestampMs)
{
    bcos::engine::PayloadAttributes attrs;
    attrs.timestamp = timestampMs;
    attrs.prevRandao =
        bcos::h256("1111111111111111111111111111111111111111111111111111111111111111");
    attrs.suggestedFeeRecipient = bcos::Address("1234567890abcdef1234567890abcdef12345678");
    attrs.parentBeaconBlockRoot =
        bcos::h256("2222222222222222222222222222222222222222222222222222222222222222");
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes{0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    if (bcos::engine::unixSecondsFromInternalMillis(timestampMs) >= 1)
    {
        attrs.minBaseFee = 0;
    }
    return attrs;
}

struct OpProfileFixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> forkSchedule;
    EngineOpScheduler scheduler;
    StubMemPool memPool;
    StubExecutor executor;
    std::shared_ptr<StubSchedulerDelegate> delegate{std::make_shared<StubSchedulerDelegate>()};
    OpEngine engine;

    explicit OpProfileFixture(std::string_view scheduleStr)
      : forkSchedule(std::make_shared<const bcos::evm::opstack::OpForkSchedule>(
            bcos::evm::opstack::OpForkSchedule::parse(scheduleStr))),
        scheduler(forkSchedule),
        engine(memPool, multiLayerStorage, executor, scheduler, blockFactory(),
            /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), delegate)
    {
        seedHeadBlockNumber(multiLayerStorage, kHeadHash, kHeadNumber);
        seedHeadBlockNumber(multiLayerStorage, kSafeHash, kHeadNumber);
        seedHeadBlockNumber(multiLayerStorage, kFinalizedHash, kHeadNumber);
    }

    bcos::engine::ForkchoiceUpdatedResult buildPayload(uint64_t timestampMs)
    {
        auto forkchoice = makeForkchoiceState();
        auto attrs = makeOpAttrs(timestampMs);
        return bcos::task::syncWait(engine.updateForkchoice(forkchoice, &attrs, 3));
    }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineBranchSmokeSuite)

BOOST_AUTO_TEST_CASE(OpModeInstantiatesAndGatesV4)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS storage(checkpointBackend);

    EngineOpScheduler scheduler(std::make_shared<const bcos::evm::opstack::OpForkSchedule>(
        bcos::evm::opstack::OpForkSchedule::parse("0:isthmus")));
    StubMemPool memPool;
    StubExecutor executor;

    OpEngine engine(memPool, storage, executor, scheduler, blockFactory(),
        /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit,
        static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), /*delegate=*/nullptr);

    bcos::engine::NewPayloadRequest request;
    request.executionPayload.timestamp = 1000;
    request.executionPayload.blockNumber = 1;
    request.executionPayload.rawTransactions = std::vector<bcos::bytes>{};
    request.executionPayload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = bcos::h256{};
    request.executionPayload.excessBlobGas = bcos::u256(0);
    request.executionPayload.blobGasUsed = bcos::u256(0);
    request.parentBeaconBlockRoot = bcos::h256{};

    BOOST_CHECK_THROW(
        bcos::task::syncWait(engine.newPayload(request, 3)), bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(OpForkProfileMatrixSuite)

BOOST_AUTO_TEST_CASE(jovian_fcu_v3_get_payload_v4_ok_v5_rejects_fcu_v4_rejects)
{
    OpProfileFixture fixture("0:isthmus,1:jovian");
    constexpr uint64_t jovianMs = 1000;

    auto forkchoice = makeForkchoiceState();
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.engine.updateForkchoice(forkchoice, nullptr, 4)),
        bcos::engine::UnsupportedFork);

    auto result = fixture.buildPayload(jovianMs);
    BOOST_REQUIRE(result.payloadId.has_value());

    BOOST_CHECK_NO_THROW(bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 4)));
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 5)),
        bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_CASE(karst_fcu_v3_get_payload_v5_ok_v4_rejects_fcu_v4_rejects)
{
    OpProfileFixture fixture("0:isthmus,1:jovian,2:karst");
    constexpr uint64_t karstMs = 2000;

    auto forkchoice = makeForkchoiceState();
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.engine.updateForkchoice(forkchoice, nullptr, 4)),
        bcos::engine::UnsupportedFork);

    auto result = fixture.buildPayload(karstMs);
    BOOST_REQUIRE(result.payloadId.has_value());

    BOOST_CHECK_NO_THROW(bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 5)));
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 4)),
        bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_CASE(cached_payload_profile_uses_build_timestamp_not_later_fork)
{
    // Build at Jovian (sec=1); schedule Karst activates at sec=2. Cache entry must keep V4 profile.
    OpProfileFixture fixture("0:isthmus,1:jovian,2:karst");
    constexpr uint64_t jovianMs = 1000;

    auto result = fixture.buildPayload(jovianMs);
    BOOST_REQUIRE(result.payloadId.has_value());

    BOOST_CHECK_NO_THROW(bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 4)));
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 5)),
        bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_CASE(fcu_v4_rejection_leaves_no_payload_cache_entry)
{
    OpProfileFixture fixture("0:isthmus,1:jovian,2:karst");
    auto forkchoice = makeForkchoiceState();
    auto attrs = makeOpAttrs(2000);

    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.engine.updateForkchoice(forkchoice, &attrs, 4)),
        bcos::engine::UnsupportedFork);

    // A follow-up V3 build must succeed — no stale cache from the rejected V4 call.
    auto result = fixture.buildPayload(2000);
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 5)));
}

BOOST_AUTO_TEST_CASE(jovian_new_payload_v4_accepts_built_payload)
{
    OpProfileFixture fixture("0:isthmus,1:jovian");
    auto result = fixture.buildPayload(1000);
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 4));
    BOOST_REQUIRE(payload);

    bcos::engine::NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bcos::bytes>{};
    auto status = bcos::task::syncWait(fixture.engine.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_CASE(karst_new_payload_v4_accepts_built_payload)
{
    OpProfileFixture fixture("0:isthmus,1:jovian,2:karst");
    auto result = fixture.buildPayload(2000);
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = bcos::task::syncWait(fixture.engine.getPayload(*result.payloadId, 5));
    BOOST_REQUIRE(payload);

    bcos::engine::NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bcos::bytes>{};
    auto status = bcos::task::syncWait(fixture.engine.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(OpForkDefenseSuite)

/// Injects a fixed fork-resolution outcome for Engine defense-path tests.
template <bcos::engine::OpForkResolutionError Err>
struct ResolutionStubSeam
{
    template <class Range>
    static bcos::h256 computeTxRoot(Range const& rawTxBytes)
    {
        return EngineOpScheduler::computeTxRoot(rawTxBytes);
    }

    template <class Storage, class Executor>
    bcos::task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(Storage&,
        Executor&, bcos::protocol::BlockHeader const&, ::ranges::input_range auto&&,
        bcos::ledger::LedgerConfig const&)
    {
        co_return {};
    }

    [[nodiscard]] bcos::engine::EngineForkResolution resolveEngineForkAt(uint64_t) const
    {
        return Err;
    }

    [[nodiscard]] bcos::engine::EngineApiProfile engineApiFor(uint64_t) const
    {
        return bcos::engine::EngineApiProfile{
            .forkchoiceUpdated = bcos::engine::ApiVersion::V3,
            .getPayload = bcos::engine::ApiVersion::V5,
            .newPayload = bcos::engine::ApiVersion::V4,
        };
    }

    [[nodiscard]] bool hasDaFootprintAt(uint64_t) const { return true; }

    static bcos::bytes synthesizeL1AttributesEnvelope(bcos::engine::OpForkId) { return {}; }
};

using UnsupportedTimestampSeam =
    ResolutionStubSeam<bcos::engine::OpForkResolutionError::UnsupportedTimestamp>;
using InconsistentConfigSeam =
    ResolutionStubSeam<bcos::engine::OpForkResolutionError::InconsistentExecutionConfig>;
using UnsupportedTimestampEngine =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, UnsupportedTimestampSeam>;
using InconsistentConfigEngine =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, InconsistentConfigSeam>;

BOOST_AUTO_TEST_CASE(fcu_unsupported_timestamp_has_no_cache_side_effect)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend(backendStorage);
    MLS storage(checkpointBackend);
    seedHeadBlockNumber(storage, kHeadHash, kHeadNumber);

    StubMemPool memPool;
    StubExecutor executor;
    UnsupportedTimestampSeam scheduler;
    UnsupportedTimestampEngine engine(memPool, storage, executor, scheduler, blockFactory(),
        /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit,
        static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), nullptr);

    auto forkchoice = makeForkchoiceState();
    auto attrs = makeOpAttrs(2000);
    BOOST_CHECK_THROW(bcos::task::syncWait(engine.updateForkchoice(forkchoice, &attrs, 3)),
        bcos::engine::UnsupportedFork);
    BOOST_CHECK_THROW(bcos::task::syncWait(engine.getPayload("0x0000000000000001", 5)),
        bcos::engine::UnknownPayload);
}

BOOST_AUTO_TEST_CASE(fcu_inconsistent_config_has_no_cache_side_effect)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend(backendStorage);
    MLS storage(checkpointBackend);
    seedHeadBlockNumber(storage, kHeadHash, kHeadNumber);

    StubMemPool memPool;
    StubExecutor executor;
    InconsistentConfigSeam scheduler;
    InconsistentConfigEngine engine(memPool, storage, executor, scheduler, blockFactory(),
        /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit,
        static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), nullptr);

    auto forkchoice = makeForkchoiceState();
    auto attrs = makeOpAttrs(2000);
    BOOST_CHECK_THROW(bcos::task::syncWait(engine.updateForkchoice(forkchoice, &attrs, 3)),
        bcos::engine::OpExecutionInternalError);
    BOOST_CHECK_THROW(bcos::task::syncWait(engine.getPayload("0x0000000000000001", 5)),
        bcos::engine::UnknownPayload);
}

BOOST_AUTO_TEST_SUITE_END()
