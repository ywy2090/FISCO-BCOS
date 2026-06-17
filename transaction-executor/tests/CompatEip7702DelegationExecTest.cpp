/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-7702 delegated CALL E2E tests (geth TestEIP7702 / TestDelegatedAccountAccessCost).
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "Eip7702TestHelpers.h"
#include "TestMemoryStorage.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/Eip7702Common.h"
#include "bcos-transaction-executor/gas/EthTxGasSettlement.h"

using bcos::task::syncWait;
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::gas;
using namespace bcos::test::eip7702;
using bcos::ledger::account::EVMAccount;

namespace bcos::test
{

namespace
{
evmc_address evmcAddr19(uint8_t suffix)
{
    evmc_address a{};
    a.bytes[19] = suffix;
    return a;
}

Address address19(uint8_t suffix)
{
    evmc_address a = evmcAddr19(suffix);
    Address out;
    std::memcpy(out.data(), a.bytes, sizeof(a.bytes));
    return out;
}
}  // namespace

class CompatEip7702DelegationExecFixture
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    TransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl(), precompiledManager};
    bcostars::protocol::BlockHeaderImpl blockHeader;

    CompatEip7702DelegationExecFixture()
    {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
        setPragueFeatures(ledgerConfig);
        setLedgerChainId(ledgerConfig, 1);
        ledgerConfig.setGasPrice({"1", 0});
        blockHeader.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*cryptoSuite->hashImpl());
    }

    task::Task<void> fundSender(evmc_address const& sender, u256 balance = u256(1) << 96)
    {
        EVMAccount<decltype(storage)> account(storage, sender, false);
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        co_await account.setBalance(balance);
        co_await account.setNonce("0");
    }
};

BOOST_FIXTURE_TEST_SUITE(CompatEip7702DelegationExec, CompatEip7702DelegationExecFixture)

BOOST_AUTO_TEST_CASE(delegated_call_writes_storage_slot)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x33);

        {
            EVMAccount<decltype(storage)> targetAcct(storage, target, false);
            co_await targetAcct.create();
            auto const& code = storageWriterBytecode();
            auto const codeHash =
                cryptoSuite->hashImpl()->hash(bytesConstRef(code.data(), code.size()));
            co_await targetAcct.setCode(code, std::string{}, codeHash);
        }

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x01);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(
            *cryptoSuite, {auth}, sender, authority, bytes{}, 0, 500'000);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, 0, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        EVMAccount<decltype(storage)> targetAcct(storage, target, false);
        evmc_bytes32 slot{};
        auto const stored = co_await targetAcct.storage(slot);
        BOOST_CHECK_EQUAL(stored.bytes[31], 0x2a);
    }());
}

BOOST_AUTO_TEST_CASE(delegated_account_call_gas_near_geth_5455)
{
    syncWait([this]() -> task::Task<void> {
        // geth runtime_test.go TestDelegatedAccountAccessCost: warm delegated CALL ≈ 5455.
        constexpr uint64_t kGethDelegatedWarmCallGas = 5455;
        constexpr uint64_t kEip7702IntrinsicOneAuth =
            gas::TX_BASE_GAS + executor_v1::EIP_7702_PER_EMPTY_ACCOUNT_COST;
        constexpr uint64_t kGasTolerance = 500;

        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x44);

        {
            EVMAccount<decltype(storage)> targetAcct(storage, target, false);
            co_await targetAcct.create();
            static bcos::bytes const stopCode{0x00};
            auto const codeHash =
                cryptoSuite->hashImpl()->hash(bytesConstRef(stopCode.data(), stopCode.size()));
            co_await targetAcct.setCode(stopCode, std::string{}, codeHash);
        }

        co_await setDelegationIndicator(storage, cryptoSuite->hashImpl(), authorityEvmc, target);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.setNonce("1");

        // Wire auth present for intrinsic (46000) but nonce mismatch => apply skipped.
        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x02);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(
            *cryptoSuite, {auth}, sender, authority, bytes{}, 0, 500'000);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, 0, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        auto const gasUsed = receipt->gasUsed().convert_to<uint64_t>();
        BOOST_CHECK_GE(gasUsed, kEip7702IntrinsicOneAuth + kGethDelegatedWarmCallGas - kGasTolerance);
        BOOST_CHECK_LE(gasUsed, kEip7702IntrinsicOneAuth + kGethDelegatedWarmCallGas + kGasTolerance);
    }());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
