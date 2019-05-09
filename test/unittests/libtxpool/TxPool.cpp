/*
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
 */

/**
 * @brief: unit test for transaction pool
 * @file: TxPool.cpp
 * @author: yujiechen
 * @date: 2018-09-25
 */
#include "FakeBlockChain.h"
#include <libdevcrypto/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::txpool;
using namespace dev::blockchain;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TxPoolTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testSessionRead)
{
    TxPoolFixture pool_test(5, 5);
    BOOST_CHECK(!!pool_test.m_txPool);
    BOOST_CHECK(!!pool_test.m_topicService);
    BOOST_CHECK(!!pool_test.m_blockChain);
    ba::io_service m_ioservice(2);
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    // std::shared_ptr<FakeSocket> fake_socket = std::make_shared<FakeSocket>(m_ioservice,
    // m_endpoint);
    /// NodeID m_nodeId = KeyPair::create().pub();
    /// start peer session and doRead
    // BOOST_REQUIRE_NO_THROW(pool_test.m_host->startPeerSession(m_nodeId, fake_socket));
}

BOOST_AUTO_TEST_CASE(testImportAndSubmit)
{
    TxPoolFixture pool_test(5, 5);
    BOOST_CHECK(!!pool_test.m_txPool);
    BOOST_CHECK(!!pool_test.m_topicService);
    BOOST_CHECK(!!pool_test.m_blockChain);
    std::shared_ptr<dev::ThreadPool> threadPool =
        std::make_shared<dev::ThreadPool>("SessionCallBackThreadPool", 2);
    // pool_test.m_host->setThreadPool(threadPool);

    Transactions trans =
        pool_test.m_blockChain->getBlockByHash(pool_test.m_blockChain->numberHash(0))
            ->transactions();
    trans[0].setBlockLimit(pool_test.m_blockChain->number() + u256(1));
    Signature sig = sign(pool_test.m_blockChain->m_sec, trans[0].sha3(WithoutSignature));
    trans[0].updateSignature(SignatureStruct(sig));
    bytes trans_data;
    trans[0].encode(trans_data);
    /// import invalid transaction
    ImportResult result = pool_test.m_txPool->import(ref(trans_data));
    BOOST_CHECK(result == ImportResult::TransactionNonceCheckFail);
    /// submit invalid transaction
    Transaction tx(trans_data, CheckTransaction::Everything);
    BOOST_CHECK_THROW(pool_test.m_txPool->submit(tx), TransactionRefused);
    Transactions transaction_vec =
        pool_test.m_blockChain->getBlockByHash(pool_test.m_blockChain->numberHash(0))
            ->transactions();
    /// set valid nonce
    size_t i = 0;
    for (auto& tx : transaction_vec)
    {
        tx.setNonce(tx.nonce() + u256(i) + u256(1));
        tx.setBlockLimit(pool_test.m_blockChain->number() + u256(1));
        bytes trans_bytes2;
        tx.encode(trans_bytes2);
        BOOST_CHECK(pool_test.m_txPool->import(ref(trans_bytes2)) == ImportResult::Malformed);
        /// resignature
        sig = sign(pool_test.m_blockChain->m_sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        tx.encode(trans_bytes2);
        result = pool_test.m_txPool->import(ref(trans_bytes2));
        BOOST_CHECK(result == ImportResult::Success);
        i++;
    }
    BOOST_CHECK(pool_test.m_txPool->pendingSize() == 5);
    /// test ordering of txPool
    Transactions pending_list = pool_test.m_txPool->pendingList();
    for (unsigned int i = 1; i < pool_test.m_txPool->pendingSize(); i++)
    {
        BOOST_CHECK(pending_list[i - 1].importTime() <= pending_list[i].importTime());
    }
    /// test out of limit, clear the queue
    tx.setNonce(u256(tx.nonce() + u256(10)));
    tx.setBlockLimit(pool_test.m_blockChain->number() + u256(1));
    sig = sign(pool_test.m_blockChain->m_sec, tx.sha3(WithoutSignature));
    tx.updateSignature(SignatureStruct(sig));
    tx.encode(trans_data);
    pool_test.m_txPool->setTxPoolLimit(5);
    result = pool_test.m_txPool->import(ref(trans_data));
    BOOST_CHECK(pool_test.m_txPool->pendingSize() == 5);
    BOOST_CHECK(result == ImportResult::TransactionPoolIsFull);
    /// test status
    TxPoolStatus m_status = pool_test.m_txPool->status();
    BOOST_CHECK(m_status.current == 5);
    BOOST_CHECK(m_status.dropped == 0);
    /// test drop
    bool ret = pool_test.m_txPool->drop(pending_list[0].sha3());
    BOOST_CHECK(ret == true);
    BOOST_CHECK(pool_test.m_txPool->pendingSize() == 4);
    m_status = pool_test.m_txPool->status();
    BOOST_CHECK(m_status.current == 4);
    BOOST_CHECK(m_status.dropped == 1);

    /// test topTransactions
    Transactions top_transactions = pool_test.m_txPool->topTransactions(20);
    BOOST_CHECK(top_transactions.size() == pool_test.m_txPool->pendingSize());
    h256Hash avoid;
    for (size_t i = 0; i < pool_test.m_txPool->pendingList().size(); i++)
        avoid.insert(pool_test.m_txPool->pendingList()[i].sha3());
    top_transactions = pool_test.m_txPool->topTransactions(20, avoid);
    BOOST_CHECK(top_transactions.size() == 0);
    /// check getProtocol id
    BOOST_CHECK(
        pool_test.m_txPool->getProtocolId() == getGroupProtoclID(1, dev::eth::ProtocolID::TxPool));
    BOOST_CHECK(pool_test.m_txPool->maxBlockLimit() == 1000);
    pool_test.m_txPool->setMaxBlockLimit(100);
    BOOST_CHECK(pool_test.m_txPool->maxBlockLimit() == 100);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
