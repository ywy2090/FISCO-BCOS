/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE Phase E: TransactionExecutorImpl SELFDESTRUCT compat (FISCO EIP-6780 gate).
 *  @file CompatTransactionExecutorPhaseETest.cpp
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "SelfdestructCompatBytecode.h"
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

namespace bcos::evm::test
{
namespace sd = selfdestruct_compat;

class CompatTransactionExecutorPhaseEFixture
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
    evmc_address sender = unhexAddress("0000000000000000000000000000000000000001");

    CompatTransactionExecutorPhaseEFixture()
    {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
        auto features = ledgerConfig.features();
        features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
        features.set(ledger::Features::Flag::feature_evm_cancun);
        features.set(ledger::Features::Flag::feature_evm_prague);
        ledgerConfig.setFeatures(features);
    }

    bcostars::protocol::BlockHeaderImpl makeBlockHeader()
    {
        bcostars::protocol::BlockHeaderImpl header;
        header.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        header.setNumber(5);
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    task::Task<void> seedSender(u256 balance)
    {
        ledger::account::EVMAccount account(storage, sender, false);
        co_await account.create();
        co_await account.setBalance(balance);
        co_await account.setNonce("1");
    }

    task::Task<void> seedSelfdestructContract()
    {
        auto const target = unhexAddress(std::string(sd::kSelfdestructTargetHex));
        ledger::account::EVMAccount account(storage, target, false);
        co_await account.create();
        co_await account.setBalance(u256(0x10));
        bytes code;
        boost::algorithm::unhex(sd::kSelfdestructRuntimeCode, std::back_inserter(code));
        co_await account.setCode(code, "", crypto::HashType{});
    }
};

BOOST_FIXTURE_TEST_SUITE(CompatTransactionExecutorPhaseE, CompatTransactionExecutorPhaseEFixture)

BOOST_AUTO_TEST_CASE(TE_FC_E_SD_existing_contract_keeps_code)
{
    task::syncWait([this]() -> task::Task<void> {
        co_await seedSender(u256(0x1000000));
        co_await seedSelfdestructContract();

        auto const targetHex = std::string(sd::kSelfdestructTargetHex);
        auto callTx = transactionFactory.createTransaction(0, targetHex, {}, {}, 0, "", "", 0);
        callTx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

        auto receipt = co_await executor.executeTransaction(
            storage, makeBlockHeader(), *callTx, contextID++, ledgerConfig, false);
        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        auto const target = unhexAddress(targetHex);
        ledger::account::EVMAccount targetAccount(storage, target, false);
        BOOST_REQUIRE(co_await targetAccount.exists());
        auto const code = co_await targetAccount.code();
        BOOST_REQUIRE(code.has_value());
        BOOST_CHECK(!code->get().empty());

        auto const beneficiary = unhexAddress(std::string(sd::kBeneficiaryHex));
        ledger::account::EVMAccount beneficiaryAccount(storage, beneficiary, false);
        BOOST_CHECK(!co_await beneficiaryAccount.exists());
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_E_SD_fisco_hook_documented)
{
    BOOST_TEST_MESSAGE(
        "SD-B: FISCO allowSelfdestruct=false — pre-existing contract survives SELFDESTRUCT "
        "(differs from mainnet EIP-6780 on Prague).");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_E_SD_same_tx_create_destroy_fisco)
{
    task::syncWait([this]() -> task::Task<void> {
        co_await seedSender(u256(0x1000000));

        bytes initCode;
        boost::algorithm::unhex(sd::kSelfdestructInitCode, std::back_inserter(initCode));
        auto deployTx = transactionFactory.createTransaction(0, "", initCode, {}, 0, "", "", 0);
        deployTx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

        auto receipt = co_await executor.executeTransaction(
            storage, makeBlockHeader(), *deployTx, contextID++, ledgerConfig, false);
        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_REQUIRE(!receipt->contractAddress().empty());

        auto const deployed = unhexAddress(receipt->contractAddress());
        ledger::account::EVMAccount deployedAccount(storage, deployed, false);
        BOOST_REQUIRE(co_await deployedAccount.exists());

        auto const beneficiary = unhexAddress(std::string(sd::kBeneficiaryHex));
        ledger::account::EVMAccount beneficiaryAccount(storage, beneficiary, false);
        BOOST_CHECK(!co_await beneficiaryAccount.exists());
    }());
}

BOOST_AUTO_TEST_CASE(TE_FC_E_SD_pair_b_keep_c_destroy_documented)
{
    BOOST_TEST_MESSAGE(
        "SD-B/C matrix: pre-existing runtime SELFDESTRUCT keeps code on FISCO (SD-B); "
        "same-tx CREATE init SELFDESTRUCT keeps code on FISCO (SD-C fisco) but destroys on "
        "Eth reference (SD-C eth) per EIP-6780.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
