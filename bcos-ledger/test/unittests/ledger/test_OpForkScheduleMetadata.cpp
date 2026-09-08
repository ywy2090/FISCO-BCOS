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
};

bool messageContains(std::exception const& e, std::string_view needle)
{
    return std::string_view(e.what()).find(needle) != std::string_view::npos;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(OpForkScheduleMetadataTest, OpForkScheduleMetadataFixture)

BOOST_AUTO_TEST_CASE(genesisPersistsScheduleMetadataTriple)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_18_0_VERSION);
        genesisConfig.m_chainID = "1";
        genesisConfig.m_groupID = "group0";
        genesisConfig.m_opstackForkSchedule = kIsthmusJovianSchedule;

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        const auto metadata = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        BOOST_REQUIRE(metadata.has_value());
        BOOST_CHECK_EQUAL(metadata->schedule, kIsthmusJovianSchedule);
        BOOST_CHECK_EQUAL(metadata->genesisHash, ledgerGenesisHash);
        BOOST_CHECK_EQUAL(metadata->scheduleHash, keccakOpForkScheduleHash(metadata->schedule));

        BOOST_CHECK_EQUAL(
            resolveOpForkScheduleCanonical(metadata, std::nullopt, false), kIsthmusJovianSchedule);
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

    BOOST_CHECK_EXCEPTION((void)resolveOpForkScheduleCanonical(stored, std::nullopt, true),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "hash mismatch"); });

    task::syncWait([this, badHash]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_18_0_VERSION);
        genesisConfig.m_chainID = "1";
        genesisConfig.m_groupID = "group0";
        genesisConfig.m_opstackForkSchedule = kIsthmusJovianSchedule;

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        storage::Entry hashEntry;
        hashEntry.set(badHash.hex());
        co_await storage2::writeOne(*storage,
            executor_v1::StateKey(std::string_view(SYS_CHAIN_METADATA), OP_FORK_SCHEDULE_HASH_KEY),
            std::move(hashEntry));

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
    BOOST_CHECK_EQUAL(resolveOpForkScheduleCanonical(std::nullopt, std::nullopt, true), "0:jovian");
    BOOST_CHECK_EQUAL(
        resolveOpForkScheduleCanonical(std::nullopt, std::nullopt, false), "0:isthmus");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
