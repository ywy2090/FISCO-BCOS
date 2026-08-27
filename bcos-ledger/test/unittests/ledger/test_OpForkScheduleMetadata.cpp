/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/ChainMetadata.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/OpForkScheduleCodec.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
constexpr char const* kMainnetSchedule = "0:isthmus,1764691201:jovian,1783526401:karst";

struct OpForkScheduleMetadataFixture
{
    OpForkScheduleMetadataFixture()
    {
        m_blockFactory = createBlockFactory(createNormalCryptoSuite());
    }

    BlockFactory::Ptr m_blockFactory;
};
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
        genesisConfig.m_opstackForkSchedule = kMainnetSchedule;

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        const auto metadata = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        BOOST_REQUIRE(metadata.has_value());
        BOOST_CHECK_EQUAL(metadata->schedule, kMainnetSchedule);
        BOOST_CHECK_EQUAL(metadata->genesisHash, ledgerGenesisHash);
        BOOST_CHECK_EQUAL(metadata->scheduleHash, keccakOpForkScheduleHash(metadata->schedule));
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
