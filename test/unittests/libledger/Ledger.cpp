/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 */

/**
 * @brief: unit test for ledger
 *
 * @file Ledger.cpp
 * @author: yujiechen
 * @date 2018-10-24
 */
#include <fisco-bcos/Fake.h>
#include <libledger/Ledger.h>
#include <libledger/LedgerManager.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::ledger;

namespace dev
{
namespace test
{
class FakeLedgerForTest : public FakeLedger
{
public:
    FakeLedgerForTest(std::shared_ptr<dev::p2p::P2PInterface> service,
        dev::GROUP_ID const& _groupId, dev::KeyPair const& _keyPair, std::string const& _baseDir)
      : FakeLedger(service, _groupId, _keyPair, _baseDir)
    {}
    /// init the ledger(called by initializer)
    bool initLedger(const std::string& _configPath) override
    {
        /// init dbInitializer
        m_dbInitializer = std::make_shared<dev::ledger::DBInitializer>(m_param);
        BOOST_CHECK(m_dbInitializer->storage() == nullptr);
        BOOST_CHECK(m_dbInitializer->stateFactory() == nullptr);
        BOOST_CHECK(m_dbInitializer->executiveContextFactory() == nullptr);
        FakeLedger::initConfig(_configPath);
        /// init blockChain
        FakeLedger::initBlockChain();
        /// intit blockVerifier
        FakeLedger::initBlockVerifier();
        /// init txPool
        FakeLedger::initTxPool();
        /// init sync
        FakeLedger::initSync();
        return true;
    }

    bool initRealLedger()
    {
        bool ret = false;
        ret = Ledger::initBlockChain();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::initBlockVerifier();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::initTxPool();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::initSync();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::consensusInitFactory();
        return ret;
    }

    void initGenesisConfig(boost::property_tree::ptree const& pt)
    {
        FakeLedger::initGenesisConfig(pt);
    }
    void init(std::string const& _configPath) { return FakeLedger::initConfig(_configPath); }
    void initMark() { FakeLedger::initMark(); }
    void initIniConfig(std::string const& iniConfigFileName)
    {
        return FakeLedger::initIniConfig(iniConfigFileName);
    }

    void initDBConfig(boost::property_tree::ptree const& pt)
    {
        return FakeLedger::initDBConfig(pt);
    }

    void setDBInitializer(std::shared_ptr<dev::ledger::DBInitializer> _dbInitializer)
    {
        m_dbInitializer = _dbInitializer;
    }
};

BOOST_FIXTURE_TEST_SUITE(LedgerTest, TestOutputHelperFixture)

/// test init ini config and genesis config
BOOST_AUTO_TEST_CASE(testGensisConfig)
{
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    dev::GROUP_ID groupId = 10;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/group.10.genesis";
    FakeLedgerForTest fakeLedger(txpool_creator.m_topicService, groupId, key_pair, "");
    fakeLedger.init(configurationPath);
    std::shared_ptr<LedgerParam> param =
        std::dynamic_pointer_cast<LedgerParam>(fakeLedger.getParam());
    /// check consensus params
    BOOST_CHECK(param->mutableConsensusParam().consensusType == "raft");
    BOOST_CHECK(param->mutableConsensusParam().maxTransactions == 2000);
    BOOST_CHECK(toHex(param->mutableConsensusParam().sealerList[0]) ==
                "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe63"
                "7191cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61");
    BOOST_CHECK(toHex(param->mutableConsensusParam().sealerList[1]) ==
                "46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3"
                "da6fc491cc1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1");
    /// check state DB param
    BOOST_CHECK(param->mutableStorageParam().type == "sql");
    BOOST_CHECK(param->mutableStateParam().type == "mpt");

    /// check timestamp
    /// init genesis configuration
    boost::property_tree::ptree pt;
    fakeLedger.initGenesisConfig(pt);
    BOOST_CHECK(fakeLedger.getParam()->mutableGenesisParam().timeStamp == UINT64_MAX);
    /// check with invalid timestamp
    fakeLedger.initGenesisConfig(pt);
    BOOST_CHECK(fakeLedger.getParam()->mutableGenesisParam().timeStamp == UINT64_MAX);
    /// check with valid timestamp
    pt.put("group.timestamp", 1553520855);
    pt.put("state.type", "storage");
    fakeLedger.initGenesisConfig(pt);
    BOOST_CHECK(fakeLedger.getParam()->mutableGenesisParam().timeStamp == 1553520855);

    /// check groupMark
    fakeLedger.initMark();
    std::string mark =
        "10-"
        "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe637191cc"
        "2aeb"
        "f4746846c0db2604adebf9c70c7f418d4d5a61,"
        "46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc491cc"
        "1c15"
        "f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1,-raft-";
    if (g_BCOSConfig.version() < RC3_VERSION)
    {
        mark += "sql-mpt-2000-300000000";
    }
    else
    {
        mark += "mpt-2000-300000000";
    }
    BOOST_CHECK(fakeLedger.getParam()->mutableGenesisParam().genesisMark == mark);

    /// init ini config
    configurationPath = getTestPath().string() + "/fisco-bcos-data/group.10.ini";
    fakeLedger.initIniConfig(configurationPath);
    BOOST_CHECK(fakeLedger.getParam()->mutableTxPoolParam().txPoolLimit == 150000);
    BOOST_CHECK(fakeLedger.getParam()->mutableTxParam().enableParallel == true);
    BOOST_CHECK(fakeLedger.getParam()->mutableConsensusParam().maxTTL == 3);

    /// modify state to storage(the default option)
    fakeLedger.initDBConfig(pt);
    BOOST_CHECK(fakeLedger.getParam()->mutableStorageParam().type == "RocksDB");
    BOOST_CHECK(fakeLedger.getParam()->mutableStateParam().type == "storage");
    fakeLedger.initIniConfig(configurationPath);
    BOOST_CHECK(fakeLedger.getParam()->mutableTxParam().enableParallel == true);

    /// test DBInitializer
    std::shared_ptr<dev::ledger::DBInitializer> dbInitializer =
        std::make_shared<dev::ledger::DBInitializer>(fakeLedger.getParam());
    /// init storageDB
    BOOST_CHECK(dbInitializer->storage() == nullptr);
    dbInitializer->initStorageDB();
    BOOST_CHECK(
        boost::filesystem::exists(fakeLedger.getParam()->mutableStorageParam().path) == true);
    BOOST_CHECK(dbInitializer->storage() != nullptr);
    /// create stateDB
    dev::h256 genesisHash = dev::sha3("abc");
    BOOST_CHECK(dbInitializer->stateFactory() == nullptr);
    BOOST_CHECK(dbInitializer->executiveContextFactory() == nullptr);
    /// create executiveContext and stateFactory
    dbInitializer->initState(genesisHash);
    BOOST_CHECK(dbInitializer->stateFactory() != nullptr);
    BOOST_CHECK(dbInitializer->executiveContextFactory() != nullptr);
    fakeLedger.setDBInitializer(dbInitializer);

    /// test initBlockChain
    BOOST_CHECK(fakeLedger.blockChain() == nullptr);
    /// test initBlockVerifier
    BOOST_CHECK(fakeLedger.blockVerifier() == nullptr);
    BOOST_CHECK(fakeLedger.consensus() == nullptr);
    BOOST_CHECK(fakeLedger.txPool() == nullptr);
    BOOST_CHECK(fakeLedger.sync() == nullptr);
    fakeLedger.initRealLedger();
    BOOST_CHECK(fakeLedger.blockVerifier() != nullptr);
    BOOST_CHECK(fakeLedger.blockChain() != nullptr);
    BOOST_CHECK(fakeLedger.consensus() != nullptr);
    BOOST_CHECK(fakeLedger.txPool() != nullptr);
    BOOST_CHECK(fakeLedger.sync() != nullptr);
}

/// test initLedgers of LedgerManager
BOOST_AUTO_TEST_CASE(testInitLedger)
{
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    std::shared_ptr<LedgerManager> ledgerManager = std::make_shared<LedgerManager>();
    dev::GROUP_ID groupId = 10;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/group.10.genesis";

    std::shared_ptr<LedgerInterface> ledger =
        std::make_shared<FakeLedgerForTest>(txpool_creator.m_topicService, groupId, key_pair, "");
    ledger->initLedger(configurationPath);
    ledgerManager->insertLedger(groupId, ledger);
    std::shared_ptr<LedgerParam> param =
        std::dynamic_pointer_cast<LedgerParam>(ledgerManager->getParamByGroupId(groupId));
    /// check BlockChain
    std::shared_ptr<BlockChainInterface> m_blockChain = ledgerManager->blockChain(groupId);
    std::shared_ptr<Block> block = m_blockChain->getBlockByNumber(m_blockChain->number());
    Block populateBlock;
    populateBlock.resetCurrentBlock(block->header());
    m_blockChain->commitBlock(populateBlock, nullptr);
    BOOST_CHECK(ledgerManager->blockChain(groupId)->number() == 1);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
