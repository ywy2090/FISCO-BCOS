// OpKarstReleaseGateSuite — Karst atomic release gate: schedule resolution, Osaka/EIP-7825
// semantics, Engine API profiles, and feeHistory parent DA footprint helper.
#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/ledger/ChainMetadata.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;

namespace
{
using StateKey = bcos::executor_v1::StateKey;
using StateValue = bcos::executor_v1::StateValue;
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

constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;

evmone::state::BlockInfo makeBlk()
{
    evmone::state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30'000'000;
    b.base_fee = 7;
    return b;
}

evmone::state::Transaction makeOrdinaryTx()
{
    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.gas_limit = 100'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    return tx;
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    static auto factory = [] {
        auto suite =
            std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
                std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
        return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
    }();
    return factory;
}
std::shared_ptr<const OpForkSchedule> karstSchedule()
{
    return std::make_shared<const OpForkSchedule>(
        OpForkSchedule::parse("0:isthmus,1:jovian,2:karst"));
}

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

class StubSchedulerDelegate : public bcos::scheduler::SchedulerInterface
{
public:
    void executeBlock(bcos::protocol::Block::Ptr block, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> cb) override
    {
        cb(nullptr, block->blockHeader(), false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> cb) override
    {
        cb(nullptr, nullptr);
    }
    void status(
        std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)> cb) override
    {
        cb(nullptr, nullptr);
    }
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> cb) override
    {
        cb(nullptr, nullptr);
    }
    void reset(std::function<void(bcos::Error::Ptr)> cb) override { cb(nullptr); }
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)> cb) override
    {
        cb(nullptr, {});
    }
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)> cb) override
    {
        cb(nullptr, {});
    }
    bcos::task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)> cb) override
    {
        cb(nullptr);
    }
};

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using OpEngine = bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;

const bcos::h256 kHeadHash{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
const bcos::h256 kSafeHash{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
const bcos::h256 kFinalizedHash{"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};

bcos::protocol::BlockFactory::Ptr blockFactory()
{
    static auto factory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    return factory;
}

void seedHead(MLS& storage, bcos::h256 const& hash, bcos::protocol::BlockNumber number)
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

bcos::engine::PayloadAttributes makeKarstAttrs(uint64_t timestampMs)
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
    attrs.minBaseFee = 0;
    return attrs;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpKarstReleaseGateSuite)

BOOST_AUTO_TEST_CASE(ResolveEngineForkFollowsScheduleTimestamps)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    bcos::evm::engine::OpSchedulerSeam<ViewType> seam(karstSchedule());
    BOOST_CHECK(seam.forkIdAt(0) == bcos::engine::OpForkId::Isthmus);
    BOOST_CHECK(seam.forkIdAt(1) == bcos::engine::OpForkId::Jovian);
    BOOST_CHECK(seam.forkIdAt(2) == bcos::engine::OpForkId::Karst);
    const auto resolution = seam.resolveEngineForkAt(2);
    BOOST_REQUIRE(std::holds_alternative<bcos::engine::EngineForkContext>(resolution));
    const auto& ctx = std::get<bcos::engine::EngineForkContext>(resolution);
    BOOST_CHECK(ctx.forkId == bcos::engine::OpForkId::Karst);
    BOOST_CHECK(ctx.api.getPayload == bcos::engine::ApiVersion::V5);
}

BOOST_AUTO_TEST_CASE(ResolveEngineForkRejectsTimestampBeforeNonzeroBaseline)
{
    const auto schedule = std::make_shared<const OpForkSchedule>(
        std::vector<OpForkActivation>{
            OpForkActivation{.fork = OpFork::Isthmus, .timestamp = 1000},
            OpForkActivation{.fork = OpFork::Jovian, .timestamp = 2000},
            OpForkActivation{.fork = OpFork::Karst, .timestamp = 3000},
        },
        OpForkSchedule::TestBypass{});
    bcos::evm::engine::OpSchedulerSeam<ViewType> seam(schedule);
    const auto resolution = seam.resolveEngineForkAt(500);
    BOOST_REQUIRE(std::holds_alternative<bcos::engine::OpForkResolutionError>(resolution));
    BOOST_CHECK(std::get<bcos::engine::OpForkResolutionError>(resolution) ==
                bcos::engine::OpForkResolutionError::UnsupportedTimestamp);
}

BOOST_AUTO_TEST_CASE(KarstConfigIsOsakaWithDepositExemptFlag)
{
    const auto& cfg = karstConfig();
    BOOST_CHECK(cfg.rev == EVMC_OSAKA);
    BOOST_CHECK(cfg.deposit_exempt_from_max_tx_gas);
    BOOST_CHECK(cfg.has_da_footprint);
}

BOOST_AUTO_TEST_CASE(KarstOrdinaryTxRejectsGasOverEip7825Cap)
{
    evmone::test::TestState ts;
    ts[kSender] = {
        .nonce = 0, .balance = intx::uint256{1'000'000'000'000'000'000}, .storage = {}, .code = {}};
    auto tx = makeOrdinaryTx();
    tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT + 1;
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto result = opValidate(
        ts, makeBlk(), tx, {env.data(), env.size()}, karstConfig(), OpFeeParams{}, 30'000'000);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(result));
    BOOST_CHECK(std::get<std::error_code>(result) ==
                evmone::state::make_error_code(evmone::state::MAX_GAS_LIMIT_EXCEEDED));
}

BOOST_AUTO_TEST_CASE(KarstDepositExecutesOverEip7825CapViaRunDeposit)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    evmone::test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    evmone::test::TestBlockHashes hashes;
    const int64_t overCap = static_cast<int64_t>(evmone::state::MAX_TX_GAS_LIMIT + 1);
    DepositTx dep{.source_hash = evmc::bytes32{},
        .from = kSender,
        .to = kSender,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = overCap,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto receipt = runDeposit(
        ts, makeBlk(), hashes, dep, karstConfig(), vm, 1234, overCap, makeReceiptFactory(), diff);
    BOOST_REQUIRE(receipt);
    BOOST_CHECK_EQUAL(receipt->status(), 0);
}

BOOST_AUTO_TEST_CASE(KarstEngineAcceptsFcuV3GetPayloadV5NewPayloadV4)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend(backendStorage);
    MLS storage(checkpointBackend);
    seedHead(storage, kHeadHash, 5);
    seedHead(storage, kSafeHash, 5);
    seedHead(storage, kFinalizedHash, 5);

    EngineOpScheduler scheduler(karstSchedule());
    StubMemPool memPool;
    StubExecutor executor;
    auto delegate = std::make_shared<StubSchedulerDelegate>();
    OpEngine engine(memPool, storage, executor, scheduler, blockFactory(),
        /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit,
        static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), delegate);

    auto forkchoice = bcos::engine::ForkchoiceState{kHeadHash, kSafeHash, kFinalizedHash};
    auto attrs = makeKarstAttrs(2000);
    auto fcu = bcos::task::syncWait(engine.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(fcu.payloadId.has_value());

    auto payload = bcos::task::syncWait(engine.getPayload(*fcu.payloadId, 5));
    BOOST_REQUIRE(payload);

    bcos::engine::NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bcos::bytes>{};
    auto status = bcos::task::syncWait(engine.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_CASE(FeeHistoryParentDaFootprintFromScheduleTimestamp)
{
    constexpr std::string_view schedule = "0:isthmus,1:jovian,2:karst";
    BOOST_CHECK(!bcos::ledger::opForkScheduleHasDaFootprintAt(schedule, 0));
    BOOST_CHECK(bcos::ledger::opForkScheduleHasDaFootprintAt(schedule, 1));
    BOOST_CHECK(bcos::ledger::opForkScheduleHasDaFootprintAt(schedule, 2));
}

BOOST_AUTO_TEST_CASE(CalcOpBaseFeeUsesParentDaFootprintNotExtraDataLength)
{
    auto headerFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite())->blockHeaderFactory();
    auto parent = headerFactory->createBlockHeader();
    parent->setGasLimit(30'000'000);
    parent->setGasUsed(10'000'000);
    parent->setBaseFee(bcos::u256(1'000'000'000));
    bcos::bytes extra(17, 0);
    extra[0] = 0x01;
    extra[3] = 0x08;
    extra[7] = 0x02;
    parent->setExtraData(extra);
    parent->setBlobGasUsed(20'000'000);

    const auto withoutDa = bcos::engine::calcOpBaseFee(*parent, /*parentHasDaFootprint=*/false);
    const auto withDa = bcos::engine::calcOpBaseFee(*parent, /*parentHasDaFootprint=*/true);
    BOOST_CHECK(withDa > withoutDa);
}

BOOST_AUTO_TEST_SUITE_END()
