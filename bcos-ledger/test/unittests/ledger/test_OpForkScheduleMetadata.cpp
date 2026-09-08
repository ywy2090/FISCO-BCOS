/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/ChainMetadata.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/ledger/OpForkScheduleCodec.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>
#include <string_view>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
constexpr char const* kIsthmusJovianSchedule = "0:isthmus,1764691201:jovian";

struct OpForkScheduleMetadataFixture
{
    OpForkScheduleMetadataFixture()
    {
        m_blockFactory = createBlockFactory(createNormalCryptoSuite());
    }

    BlockFactory::Ptr m_blockFactory;

    static LedgerConfig emptyLedgerConfig()
    {
        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);
        return param;
    }

    static GenesisConfig scheduleGenesis(std::string_view schedule)
    {
        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_18_0_VERSION);
        genesisConfig.m_chainID = "1";
        genesisConfig.m_groupID = "group0";
        genesisConfig.m_opstackForkSchedule = std::string(schedule);
        return genesisConfig;
    }
};

bool messageContains(std::exception const& e, std::string_view needle)
{
    return std::string_view(e.what()).find(needle) != std::string_view::npos;
}

task::Task<void> writeMetadataRow(auto& storage, std::string_view key, std::string_view value)
{
    storage::Entry entry;
    entry.set(value);
    co_await storage2::writeOne(storage,
        executor_v1::StateKey(std::string_view(SYS_CHAIN_METADATA), key), std::move(entry));
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(OpForkScheduleMetadataTest, OpForkScheduleMetadataFixture)

BOOST_AUTO_TEST_CASE(genesisPersistsScheduleMetadataTriple)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(kIsthmusJovianSchedule), emptyLedgerConfig()));

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        const auto metadata = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        BOOST_REQUIRE(metadata.has_value());
        BOOST_CHECK_EQUAL(metadata->schedule, kIsthmusJovianSchedule);
        BOOST_CHECK_EQUAL(metadata->genesisHash, ledgerGenesisHash);
        BOOST_CHECK_EQUAL(metadata->scheduleHash, keccakOpForkScheduleHash(metadata->schedule));

        BOOST_CHECK_EQUAL(
            resolveOpForkScheduleCanonical(metadata, std::nullopt, false, ledgerGenesisHash),
            kIsthmusJovianSchedule);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(hashMismatchFailClosed)
{
    const auto goodHash = keccakOpForkScheduleHash(kIsthmusJovianSchedule);
    auto badHash = goodHash;
    badHash[0] ^= 0x01;

    OpForkScheduleMetadata stored{
        .schedule = kIsthmusJovianSchedule,
        .scheduleHash = badHash,
        .genesisHash = HashType{},
    };

    BOOST_CHECK_EXCEPTION(
        (void)resolveOpForkScheduleCanonical(stored, std::nullopt, true, HashType{}),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "hash mismatch"); });

    task::syncWait([this, badHash]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(kIsthmusJovianSchedule), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        co_await writeMetadataRow(*storage, OP_FORK_SCHEDULE_HASH_KEY, badHash.hex());

        bool threw = false;
        try
        {
            (void)co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        }
        catch (InvalidOpForkSchedule const& e)
        {
            threw = true;
            BOOST_CHECK(messageContains(e, "hash mismatch"));
        }
        BOOST_CHECK(threw);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(emptyMetadataFallsBackToLegacy)
{
    BOOST_CHECK_EQUAL(
        resolveOpForkScheduleCanonical(std::nullopt, std::nullopt, true, HashType{}), "0:jovian");
    BOOST_CHECK_EQUAL(
        resolveOpForkScheduleCanonical(std::nullopt, std::nullopt, false, HashType{}), "0:isthmus");
}

BOOST_AUTO_TEST_CASE(partialTripleIsNotAbsent)
{
    const auto genesisHash = HashType{};
    OpForkScheduleMetadataRows oneOfThree;
    oneOfThree.schedule = kIsthmusJovianSchedule;
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(oneOfThree, genesisHash),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "partial"); });

    OpForkScheduleMetadataRows twoOfThree;
    twoOfThree.schedule = kIsthmusJovianSchedule;
    twoOfThree.scheduleHash = keccakOpForkScheduleHash(kIsthmusJovianSchedule).hex();
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(twoOfThree, genesisHash),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "partial"); });

    task::syncWait([]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        co_await writeMetadataRow(*storage, OP_FORK_SCHEDULE_KEY, kIsthmusJovianSchedule);

        bool oneKeyThrew = false;
        try
        {
            auto const maybe = co_await readOpForkScheduleMetadata(*storage, HashType{});
            BOOST_CHECK(!maybe.has_value());
        }
        catch (InvalidOpForkSchedule const& e)
        {
            oneKeyThrew = true;
            BOOST_CHECK(messageContains(e, "partial"));
        }
        BOOST_CHECK(oneKeyThrew);

        co_await writeMetadataRow(*storage, OP_FORK_SCHEDULE_HASH_KEY,
            keccakOpForkScheduleHash(kIsthmusJovianSchedule).hex());

        bool twoKeysThrew = false;
        try
        {
            auto const maybe = co_await readOpForkScheduleMetadata(*storage, HashType{});
            BOOST_CHECK(!maybe.has_value());
        }
        catch (InvalidOpForkSchedule const& e)
        {
            twoKeysThrew = true;
            BOOST_CHECK(messageContains(e, "partial"));
        }
        BOOST_CHECK(twoKeysThrew);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(genesisBindingMismatchFailClosed)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(kIsthmusJovianSchedule), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();
        auto wrongGenesisHash = ledgerGenesisHash;
        wrongGenesisHash[0] ^= 0x01;

        bool readThrew = false;
        try
        {
            (void)co_await readOpForkScheduleMetadata(*storage, wrongGenesisHash);
        }
        catch (InvalidOpForkSchedule const& e)
        {
            readThrew = true;
            BOOST_CHECK(messageContains(e, "genesis binding mismatch"));
        }
        BOOST_CHECK(readThrew);

        const auto metadata = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        BOOST_REQUIRE(metadata.has_value());
        BOOST_CHECK_EXCEPTION(
            (void)resolveOpForkScheduleCanonical(metadata, std::nullopt, true, wrongGenesisHash),
            InvalidOpForkSchedule, [](InvalidOpForkSchedule const& e) {
                return messageContains(e, "genesis binding mismatch");
            });
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(badHexIsInvalidOpForkSchedule)
{
    const auto genesisHash = HashType{};
    OpForkScheduleMetadataRows badScheduleHash;
    badScheduleHash.schedule = kIsthmusJovianSchedule;
    badScheduleHash.scheduleHash = "not-a-hex-hash";
    badScheduleHash.genesisHash = genesisHash.hex();
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(badScheduleHash, genesisHash),
        InvalidOpForkSchedule, [](InvalidOpForkSchedule const& e) {
            return messageContains(e, "hex") || messageContains(e, "hash");
        });

    OpForkScheduleMetadataRows badGenesisHash;
    badGenesisHash.schedule = kIsthmusJovianSchedule;
    badGenesisHash.scheduleHash = keccakOpForkScheduleHash(kIsthmusJovianSchedule).hex();
    badGenesisHash.genesisHash = "gg";
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(badGenesisHash, genesisHash),
        InvalidOpForkSchedule, [](InvalidOpForkSchedule const& e) {
            return messageContains(e, "hex") || messageContains(e, "hash");
        });
}

BOOST_AUTO_TEST_CASE(persistNormalizesCanonicalText)
{
    const auto genesisHash = HashType{};
    const auto metadata = buildOpForkScheduleMetadata("0:Isthmus", genesisHash);
    BOOST_CHECK_EQUAL(metadata.schedule, "0:isthmus");
    BOOST_CHECK_EQUAL(metadata.scheduleHash, keccakOpForkScheduleHash("0:isthmus"));

    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis("0:Isthmus"), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto stored =
            co_await readOpForkScheduleMetadata(*storage, block->blockHeader()->hash());
        BOOST_REQUIRE(stored.has_value());
        BOOST_CHECK_EQUAL(stored->schedule, "0:isthmus");
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(genesisWriteRejectsKarst)
{
    task::syncWait([this]() -> task::Task<void> {
        for (auto const* schedule : {"0:karst", "0:isthmus,1:karst"})
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            bool threw = false;
            try
            {
                (void)co_await ledger::buildGenesisBlock(
                    *ledger, scheduleGenesis(schedule), emptyLedgerConfig());
            }
            catch (InvalidOpForkSchedule const&)
            {
                threw = true;
            }
            BOOST_CHECK(threw);
        }
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
