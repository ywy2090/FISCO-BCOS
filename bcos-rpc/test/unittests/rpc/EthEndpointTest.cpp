/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EthEndpointTest.cpp
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-txpool/TxPoolFactory.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <magic_enum/magic_enum.hpp>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::codec::rlp;

namespace bcos::test
{
namespace
{
struct MinimalEip7702RawTx
{
    std::string hex;
    crypto::HashType hash;
};

MinimalEip7702RawTx buildMinimalEip7702RawTx()
{
    Web3Transaction tx;
    tx.type = rpc::TransactionType::EIP7702;
    tx.chainId = 1;
    tx.nonce = 0;
    tx.maxPriorityFeePerGas = 1;
    tx.maxFeePerGas = 1;
    tx.gasLimit = 21000;
    tx.to = Address("0x0000000000000000000000000000000000000001");
    tx.value = 0;

    AuthorizationListEntry authEntry;
    authEntry.chainId = 0;
    authEntry.address = Address("0x0000000000000000000000000000000000000002");
    authEntry.nonce = 0;
    authEntry.yParity = 0;
    authEntry.r = h256(1);
    authEntry.s = h256(2);
    tx.authorizationList.push_back(authEntry);

    tx.signatureV = 0;
    tx.signatureR = h256(3).asBytes();
    tx.signatureS = h256(4).asBytes();

    MinimalEip7702RawTx out;
    out.hash = tx.txHash();
    bcos::bytes encoded;
    encode(encoded, tx);
    out.hex = "0x" + toHex(encoded);
    return out;
}

std::string buildMinimalEip7702RawTxHex()
{
    return buildMinimalEip7702RawTx().hex;
}

/// Scheduler stub returning geth-aligned EIP-7702 estimateGas floor (1 wire auth).
class Eip7702EstimateGasScheduler : public bcos::test::FakeScheduler
{
public:
    using FakeScheduler::FakeScheduler;

    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) noexcept
        override
    {
        auto holder = std::make_shared<bcostars::TransactionReceipt>();
        holder->data.status = 0;
        holder->data.gasUsed = "46000";
        auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            [holder]() { return holder.get(); });
        callback(nullptr, receipt);
    }
};

rpc::NodeService::Ptr rebuildNodeServiceWithScheduler(
    RPCFixture& fixture, bcos::test::FakeScheduler::Ptr scheduler)
{
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(fixture.cryptoSuite);
    auto txFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(fixture.cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(fixture.cryptoSuite);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        fixture.cryptoSuite, blockHeaderFactory, txFactory, receiptFactory);

    auto nodeId = std::make_shared<KeyImpl>(
        h256("1110000000000000000000000000000000000000000000000000000000000000").asBytes());
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    auto txPoolFactory = std::make_shared<TxPoolFactory>(nodeId, fixture.cryptoSuite,
        txResultFactory, blockFactory, fixture.m_frontService, fixture.m_ledger, "group0", "chain0",
        100000000, bcos::txpool::DEFAULT_POOL_LIMIT, true);
    txPoolFactory->setScheduler(scheduler);
    auto txPool = txPoolFactory->createTxPool(*fixture.ioServicePool->getIOService());
    txPool->init();
    txPool->start();

    return std::make_shared<rpc::NodeService>(
        fixture.m_ledger, scheduler, txPool, nullptr, nullptr, blockFactory);
}

rpc::NodeService::Ptr rebuildNodeServiceWithCryptoSuite(
    RPCFixture& fixture, crypto::CryptoSuite::Ptr cryptoSuite)
{
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, txFactory, receiptFactory);

    auto nodeId = std::make_shared<KeyImpl>(
        h256("1110000000000000000000000000000000000000000000000000000000000000").asBytes());
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    auto txPoolFactory = std::make_shared<TxPoolFactory>(nodeId, cryptoSuite, txResultFactory,
        blockFactory, fixture.m_frontService, fixture.m_ledger, "group0", "chain0", 100000000,
        bcos::txpool::DEFAULT_POOL_LIMIT, true);
    txPoolFactory->setScheduler(fixture.scheduler);
    auto txPool = txPoolFactory->createTxPool(*fixture.ioServicePool->getIOService());
    txPool->init();
    txPool->start();

    return std::make_shared<rpc::NodeService>(
        fixture.m_ledger, fixture.scheduler, txPool, nullptr, nullptr, blockFactory);
}

void expectSendRawRejectsEip7702(
    rpc::NodeService::Ptr const& nodeService, std::string_view expectedMessage)
{
    EthEndpoint endpoint(nodeService, nullptr, false);
    Json::Value request(Json::arrayValue);
    request.append(buildMinimalEip7702RawTxHex());
    Json::Value response;

    try
    {
        task::syncWait(endpoint.sendRawTransaction(request, response));
        BOOST_FAIL("expected JsonRpcException");
    }
    catch (JsonRpcException const& ex)
    {
        BOOST_CHECK_EQUAL(ex.code(), static_cast<int32_t>(Web3JsonRpcError::Web3DefaultError));
        BOOST_CHECK(ex.msg().find(expectedMessage) != std::string::npos);
    }
}

ledger::Features pragueEnabledFeatures()
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_evm_prague);
    return features;
}

void expectGateRejectsEip7702Call(
    rpc::NodeService::Ptr const& nodeService, std::string_view expectedMessage)
{
    EthEndpoint endpoint(nodeService, nullptr, false);
    Json::Value request(Json::arrayValue);
    Json::Value callObj;
    callObj["type"] = "0x4";
    callObj["to"] = "0x0000000000000000000000000000000000000001";
    request.append(callObj);
    request.append("latest");
    Json::Value response;

    try
    {
        task::syncWait(endpoint.call(request, response));
        BOOST_FAIL("expected JsonRpcException");
    }
    catch (JsonRpcException const& ex)
    {
        BOOST_CHECK(ex.msg().find(expectedMessage) != std::string::npos);
    }
}

void expectEip7702CallRejectsEmptyAuthList(rpc::NodeService::Ptr const& nodeService)
{
    EthEndpoint endpoint(nodeService, nullptr, false);
    Json::Value request(Json::arrayValue);
    Json::Value callObj;
    callObj["type"] = "0x4";
    callObj["to"] = "0x0000000000000000000000000000000000000001";
    callObj["authorizationList"] = Json::arrayValue;
    request.append(callObj);
    request.append("latest");
    Json::Value response;

    try
    {
        task::syncWait(endpoint.call(request, response));
        BOOST_FAIL("expected JsonRpcException");
    }
    catch (JsonRpcException const& ex)
    {
        BOOST_CHECK_EQUAL(ex.code(), static_cast<int32_t>(InvalidParams));
        BOOST_CHECK(ex.msg().find("authorization_list must not be empty") != std::string::npos);
    }
}

void expectEip7702EstimateGasRejectsEmptyAuthList(rpc::NodeService::Ptr const& nodeService)
{
    EthEndpoint endpoint(nodeService, nullptr, false);
    Json::Value request(Json::arrayValue);
    Json::Value callObj;
    callObj["type"] = "0x4";
    callObj["to"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    callObj["authorizationList"] = Json::arrayValue;
    request.append(callObj);
    request.append("latest");
    Json::Value response;

    try
    {
        task::syncWait(endpoint.estimateGas(request, response));
        BOOST_FAIL("expected JsonRpcException");
    }
    catch (JsonRpcException const& ex)
    {
        BOOST_CHECK_EQUAL(ex.code(), static_cast<int32_t>(InvalidParams));
        BOOST_CHECK(ex.msg().find("authorization_list must not be empty") != std::string::npos);
    }
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(testEthEndpoint, RPCFixture)

BOOST_AUTO_TEST_CASE(sendRawTransaction_rejectsEip7702OnLegacyExecutor)
{
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "0");
    expectSendRawRejectsEip7702(nodeService, "type-4 unsupported on legacy executor");
}

BOOST_AUTO_TEST_CASE(sendRawTransaction_rejectsEip7702OnSm2Chain)
{
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "1");
    auto sm3 = std::make_shared<crypto::SM3>();
    auto sm2 = std::make_shared<crypto::SM2Crypto>();
    auto smCryptoSuite = std::make_shared<crypto::CryptoSuite>(sm3, sm2, nullptr);
    auto smNodeService = rebuildNodeServiceWithCryptoSuite(*this, smCryptoSuite);
    expectSendRawRejectsEip7702(smNodeService, "EIP-7702 transactions require secp256k1");
}

BOOST_AUTO_TEST_CASE(sendRawTransaction_rejectsEip7702WithoutPrague)
{
    m_ledger->setTestFeatures(ledger::Features{});
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "1");
    expectSendRawRejectsEip7702(nodeService, "feature_evm_prague");
}

BOOST_AUTO_TEST_CASE(sendRawTransaction_passesEip7702Gate_whenPragueEnabled)
{
    m_ledger->setTestFeatures(pragueEnabledFeatures());
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "1");

    auto const rawTx = buildMinimalEip7702RawTx();
    EthEndpoint endpoint(nodeService, nullptr, false);
    Json::Value request(Json::arrayValue);
    request.append(rawTx.hex);
    Json::Value response;

    task::syncWait(endpoint.sendRawTransaction(request, response));

    auto const expectedHash = rawTx.hash.hexPrefixed();
    if (response.isMember("result"))
    {
        BOOST_CHECK_EQUAL(response["result"].asString(), expectedHash);
    }
    else
    {
        BOOST_REQUIRE(response.isMember("error"));
        BOOST_REQUIRE(response["error"].isMember("data"));
        BOOST_CHECK(response["error"]["data"].isMember("txHash"));
        BOOST_CHECK_EQUAL(response["error"]["data"]["txHash"].asString(), expectedHash);
        auto const msg = response["error"]["message"].asString();
        BOOST_CHECK(msg.find("feature_evm_prague") == std::string::npos);
        BOOST_CHECK(msg.find("legacy executor") == std::string::npos);
        BOOST_CHECK(msg.find("secp256k1") == std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(call_rejectsEip7702_when_prague_off)
{
    m_ledger->setTestFeatures(ledger::Features{});
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "1");
    expectGateRejectsEip7702Call(nodeService, "feature_evm_prague");
}

BOOST_AUTO_TEST_CASE(call_rejects_type4_empty_authorization_list_when_prague_on)
{
    m_ledger->setTestFeatures(pragueEnabledFeatures());
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "1");
    expectEip7702CallRejectsEmptyAuthList(nodeService);
}

BOOST_AUTO_TEST_CASE(estimateGas_rejects_type4_empty_authorization_list_when_prague_on)
{
    m_ledger->setTestFeatures(pragueEnabledFeatures());
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "1");
    expectEip7702EstimateGasRejectsEmptyAuthList(nodeService);
}

BOOST_AUTO_TEST_CASE(estimateGas_passesEip7702Gate_whenPragueEnabled)
{
    m_ledger->setTestFeatures(pragueEnabledFeatures());
    m_ledger->setSystemConfig(
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)), "1");

    auto estimateScheduler =
        std::make_shared<Eip7702EstimateGasScheduler>(m_ledger, m_blockFactory);
    auto estimateNodeService = rebuildNodeServiceWithScheduler(*this, estimateScheduler);

    EthEndpoint endpoint(estimateNodeService, nullptr, false);
    Json::Value request(Json::arrayValue);
    Json::Value callObj;
    callObj["type"] = "0x4";
    callObj["to"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    callObj["authorizationList"] = Json::arrayValue;
    callObj["authorizationList"].append(Json::objectValue);
    request.append(callObj);
    request.append("latest");
    Json::Value response;

    task::syncWait(endpoint.estimateGas(request, response));
    BOOST_CHECK(response.isMember("result"));
    BOOST_CHECK(!response.isMember("error"));
    BOOST_CHECK_EQUAL(response["result"].asString(), toQuantity(46000ULL));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
