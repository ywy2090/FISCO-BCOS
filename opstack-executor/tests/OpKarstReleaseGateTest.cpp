// OpKarstReleaseGateSuite — Karst atomic release gate: schedule resolution + API profile.
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/test/unit_test.hpp>

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
}  // namespace

BOOST_AUTO_TEST_SUITE(OpKarstReleaseGateSuite)

BOOST_AUTO_TEST_CASE(ResolveEngineForkFollowsScheduleTimestamps)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    const auto schedule =
        std::make_shared<const OpForkSchedule>(OpForkSchedule::parse("0:isthmus,1:jovian,2:karst"));
    bcos::evm::engine::OpSchedulerSeam<ViewType> seam(schedule);
    BOOST_CHECK(seam.forkIdAt(0) == bcos::engine::OpForkId::Isthmus);
    BOOST_CHECK(seam.forkIdAt(1) == bcos::engine::OpForkId::Jovian);
    BOOST_CHECK(seam.forkIdAt(2) == bcos::engine::OpForkId::Karst);
    const auto resolution = seam.resolveEngineForkAt(2);
    BOOST_REQUIRE(std::holds_alternative<bcos::engine::EngineForkContext>(resolution));
    const auto& ctx = std::get<bcos::engine::EngineForkContext>(resolution);
    BOOST_CHECK(ctx.forkId == bcos::engine::OpForkId::Karst);
    BOOST_CHECK(ctx.api.getPayload == bcos::engine::ApiVersion::V5);
}

BOOST_AUTO_TEST_CASE(KarstConfigIsOsakaWithDepositExemptFlag)
{
    const auto& cfg = karstConfig();
    BOOST_CHECK(cfg.rev == EVMC_OSAKA);
    BOOST_CHECK(cfg.deposit_exempt_from_max_tx_gas);
    BOOST_CHECK(cfg.has_da_footprint);
}

BOOST_AUTO_TEST_CASE(KarstEngineApiProfileAdvertisesV3V5V4)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    const auto schedule =
        std::make_shared<const OpForkSchedule>(OpForkSchedule::parse("0:isthmus,1:jovian,2:karst"));
    bcos::evm::engine::OpSchedulerSeam<ViewType> seam(schedule);
    const auto profile = seam.engineApiFor(2);
    BOOST_CHECK(profile.forkchoiceUpdated == bcos::engine::ApiVersion::V3);
    BOOST_CHECK(profile.getPayload == bcos::engine::ApiVersion::V5);
    BOOST_CHECK(profile.newPayload == bcos::engine::ApiVersion::V4);
    BOOST_CHECK(seam.forkIdAt(2) == bcos::engine::OpForkId::Karst);
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
