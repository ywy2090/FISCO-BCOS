/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE Phase D: TransactionExecutorImpl compat executive smoke tests.
 *  @file CompatTransactionExecutorPhaseDTest.cpp
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestMemoryStorage.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-task/Wait.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <string_view>

namespace bcos::evm::test
{
namespace
{
constexpr std::string_view kHelloWorldCreationBin =
    "60806040526040805190810160405280600181526020017f3100000000000000000000000000000000000000"
    "0000000000000000000000008152506001908051906020019061004f9291906100ae565b5034801561005c5760"
    "0080fd5b506040805190810160405280600d81526020017f48656c6c6f2c20576f726c64210000000000000000"
    "0000000000000000000000815250600090805190602001906100a89291906100ae565b50610153565b82805460"
    "0181600116156101000203166002900490600052602060002090601f016020900481019282601f106100ef5780"
    "5160ff191683800117855561011d565b8280016001018555821561011d579182015b8281111561011c57825182"
    "5591602001919060010190610101565b5b50905061012a919061012e565b5090565b61015091905b8082111561"
    "014c576000816000905550600101610134565b5090565b90565b6104ac806101626000396000f3006080604052"
    "60043610610057576000357c0100000000000000000000000000000000000000000000000000000000900463ff"
    "ffffff1680634ed3885e1461005c57806354fd4d50146100c55780636d4ce63c14610155575b600080fd5b3480"
    "1561006857600080fd5b506100c3600480360381019080803590602001908201803590602001908080601f0160"
    "208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192"
    "9192905050506101e5565b005b3480156100d157600080fd5b506100da61029b565b6040518080602001828103"
    "825283818151815260200191508051906020019080838360005b8381101561011a578082015181840152602081"
    "0190506100ff565b50505050905090810190601f1680156101475780820380516001836020036101000a031916"
    "815260200191505b509250505060405180910390f35b34801561016157600080fd5b5061016a610339565b6040"
    "518080602001828103825283818151815260200191508051906020019080838360005b838110156101aa578082"
    "01518184015260208101905061018f565b50505050905090810190601f1680156101d757808203805160018360"
    "20036101000a031916815260200191505b509250505060405180910390f35b80600090805190602001906101fb"
    "9291906103db565b507f93a093529f9c8a0c300db4c55fcd27c068c4f5e0e8410bc288c7e76f3d71083e816040"
    "518080602001828103825283818151815260200191508051906020019080838360005b8381101561025e578082"
    "015181840152602081019050610243565b50505050905090810190601f16801561028b57808203805160018360"
    "20036101000a031916815260200191505b509250505060405180910390a150565b600180546001816001161561"
    "01000203166002900480601f016020809104026020016040519081016040528092919081815260200182805460"
    "0181600116156101000203166002900480156103315780601f1061030657610100808354040283529160200191"
    "610331565b820191906000526020600020905b81548152906001019060200180831161031457829003601f1682"
    "01915b505050505081565b606060008054600181600116156101000203166002900480601f0160208091040260"
    "200160405190810160405280929190818152602001828054600181600116156101000203166002900480156103"
    "d15780601f106103a6576101008083540402835291602001916103d1565b820191906000526020600020905b81"
    "54815290600101906020018083116103b457829003601f168201915b5050505050905090565b82805460018160"
    "0116156101000203166002900490600052602060002090601f016020900481019282601f1061041c57805160ff"
    "191683800117855561044a565b8280016001018555821561044a579182015b8281111561044957825182559160"
    "200191906001019061042e565b5b509050610457919061045b565b5090565b61047d91905b8082111561047957"
    "6000816000905550600101610461565b5090565b905600a165627a7a723058204736027ad6b97d7cd2685379ac"
    "b35b386dcb18799934be8283f1e08cd1f0c6ec0029";

constexpr std::string_view kSetFiscoInput =
    "4ed3885e000000000000000000000000000000000000000000000000000000000000002000000000000000000000"
    "0000000000000000000000000000000000000000000005666973636f000000000000000000000000000000000000"
    "00000000000000000000";

constexpr std::string_view kMcopyContractBin =
    "6080604052348015600e575f80fd5b5060b980601a5f395ff3fe6080604052348015600e575f80fd5b50600436"
    "106026575f3560e01c80632dbaeee914602a575b5f80fd5b60306044565b604051603b9190606c565b60405180"
    "910390f35b5f60506020526020805f5e5f51905090565b5f819050919050565b6066816056565b82525050565b"
    "5f602082019050607d5f830184605f565b9291505056fea2646970667358221220c16107fa00317d2d630d4d01"
    "9754eb2bae42e96482d0050308e60ec21c69d7eb64736f6c63430008190033";
}  // namespace

class CompatTransactionExecutorPhaseDFixture
{
public:
    executor_v1::MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    bcos::evm::PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    bcos::evm::TransactionExecutorImpl<> executor{
        receiptFactory, cryptoSuite->hashImpl(), precompiledManager};
    int contextID = 0;
    evmc_address sender = unhexAddress("e0e794ca86d198042b64285c5ce667aee747509b");

    CompatTransactionExecutorPhaseDFixture()
    {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
    }

    bcostars::protocol::BlockHeaderImpl makeBlockHeader()
    {
        bcostars::protocol::BlockHeaderImpl header;
        header.setVersion(static_cast<uint32_t>(protocol::BlockVersion::V3_0_VERSION));
        header.setNumber(1);
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    task::Task<void> seedSenderBalance(u256 balance)
    {
        ledger::account::EVMAccount account(storage, sender, false);
        co_await account.create();
        co_await account.setBalance(balance);
    }
};

BOOST_FIXTURE_TEST_SUITE(CompatTransactionExecutorPhaseD, CompatTransactionExecutorPhaseDFixture)

BOOST_AUTO_TEST_CASE(TE_FC_D_S_cancun_only_deploy_call)
{
    task::syncWait([this]() -> task::Task<void> {
        auto features = ledgerConfig.features();
        features.setGenesisFeatures(protocol::BlockVersion::V3_0_VERSION);
        features.set(ledger::Features::Flag::feature_evm_cancun);
        ledgerConfig.setFeatures(features);

        co_await seedSenderBalance(1'000'000'000);
        auto blockHeader = makeBlockHeader();

        bytes deployInput;
        boost::algorithm::unhex(kHelloWorldCreationBin, std::back_inserter(deployInput));
        auto deployTx = transactionFactory.createTransaction(0, "", deployInput, {}, 0, "", "", 0);
        deployTx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

        auto deployReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *deployTx, contextID++, ledgerConfig, false);
        BOOST_REQUIRE(deployReceipt);
        BOOST_CHECK_EQUAL(deployReceipt->status(), 0);
        BOOST_CHECK(!deployReceipt->contractAddress().empty());

        bytes callInput;
        boost::algorithm::unhex(kSetFiscoInput, std::back_inserter(callInput));
        auto callTx = transactionFactory.createTransaction(
            0, std::string(deployReceipt->contractAddress()), callInput, {}, 0, "", "", 0);
        callTx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

        auto callReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *callTx, contextID++, ledgerConfig, false);
        BOOST_REQUIRE(callReceipt);
        BOOST_CHECK_EQUAL(callReceipt->status(), 0);
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_D_S_london_mcopy_cancun_floor_documented)
{
    task::syncWait([this]() -> task::Task<void> {
        BOOST_TEST_MESSAGE(
            "FC-D: legacy executive rejected MCOPY on London; FiscoPolicy floors revision at "
            "EVMC_CANCUN, so MCOPY remains available without feature_evm_cancun.");

        auto features = ledgerConfig.features();
        features.setGenesisFeatures(protocol::BlockVersion::V3_0_VERSION);
        ledgerConfig.setFeatures(features);

        co_await seedSenderBalance(1'000'000'000);
        auto blockHeader = makeBlockHeader();

        bytes deployInput;
        boost::algorithm::unhex(kMcopyContractBin, std::back_inserter(deployInput));
        auto deployTx = transactionFactory.createTransaction(0, "", deployInput, {}, 0, "", "", 0);
        deployTx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

        auto deployReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *deployTx, contextID++, ledgerConfig, false);
        BOOST_REQUIRE(deployReceipt);
        BOOST_CHECK_EQUAL(deployReceipt->status(), 0);

        bytes callInput = bcos::fromHex("2dbaeee9");
        auto callTx = transactionFactory.createTransaction(
            0, std::string(deployReceipt->contractAddress()), callInput, {}, 0, "", "", 0);
        callTx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

        auto callReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *callTx, contextID++, ledgerConfig, false);
        BOOST_REQUIRE(callReceipt);
        BOOST_CHECK_EQUAL(callReceipt->status(), 0);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
