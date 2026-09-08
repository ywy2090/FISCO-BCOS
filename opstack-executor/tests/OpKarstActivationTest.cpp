// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Q5: Jovian+ activation blocks are deposits-only, driven by the timestamp schedule
// (not the 176-byte L1-attributes heuristic). Karst profiles use TestBypass only.

#include "support/KarstNutHelpers.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/engine/OpTime.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
#include <boost/test/unit_test.hpp>
#include <cstring>
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

BOOST_AUTO_TEST_SUITE_END()
