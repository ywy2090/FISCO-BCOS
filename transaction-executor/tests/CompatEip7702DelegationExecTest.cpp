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

        // Delegated code runs in the authority account's storage context (EIP-7702).
        EVMAccount<decltype(storage)> authorityAcct(storage, authorityEvmc, false);
        evmc_bytes32 slot{};
        auto const stored = co_await authorityAcct.storage(slot);
        BOOST_CHECK_EQUAL(stored.bytes[31], 0x2a);
    }());
}

BOOST_AUTO_TEST_CASE(geth_TestEIP7702_chained_delegation_writes_slot_42)
{
    syncWait([this]() -> task::Task<void> {
        // Port of geth core/blockchain_test.go TestEIP7702:
        // addr1 -> delegate aa -> CALL addr2 -> delegate bb -> SSTORE(0x42, 0x42).
        auto const key1 = gethEip7702Key1();
        auto const key2 = gethEip7702Key2();
        auto const addr1 = authorityAddressFromKey(cryptoSuite->hashImpl(), key1);
        auto const addr2 = authorityAddressFromKey(cryptoSuite->hashImpl(), key2);
        auto const addr1Evmc = executor::addressToEvmc(addr1);
        auto const addr2Evmc = executor::addressToEvmc(addr2);
        auto const aa = gethEip7702AddressAa();
        auto const bb = gethEip7702AddressBb();

        {
            EVMAccount<decltype(storage)> aaAcct(storage, aa, false);
            co_await aaAcct.create();
            auto const aaCode = callWithValue1Bytecode(addr2);
            auto const aaHash =
                cryptoSuite->hashImpl()->hash(bytesConstRef(aaCode.data(), aaCode.size()));
            co_await aaAcct.setCode(aaCode, std::string{}, aaHash);
        }
        {
            EVMAccount<decltype(storage)> bbAcct(storage, bb, false);
            co_await bbAcct.create();
            auto const bbCode = sstoreSlotValueBytecode(0x42, 0x42);
            auto const bbHash =
                cryptoSuite->hashImpl()->hash(bytesConstRef(bbCode.data(), bbCode.size()));
            co_await bbAcct.setCode(bbCode, std::string{}, bbHash);
        }

        {
            EVMAccount<decltype(storage)> addr1Acct(storage, addr1Evmc, false);
            co_await addr1Acct.create();
            co_await addr1Acct.setNonce("0");
            co_await addr1Acct.setBalance(u256(1) << 96);
        }
        {
            EVMAccount<decltype(storage)> addr2Acct(storage, addr2Evmc, false);
            co_await addr2Acct.create();
            co_await addr2Acct.setNonce("0");
            co_await addr2Acct.setBalance(u256(1) << 96);
        }

        auto const auth1 = signAuthorizationTuple(cryptoSuite->hashImpl(), key1, 1, aa, 1);
        auto const auth2 = signAuthorizationTuple(cryptoSuite->hashImpl(), key2, 0, bb, 0);

        auto tx = makeWeb3Type4Transaction(
            *cryptoSuite, {auth1, auth2}, addr1Evmc, addr1, bytes{}, 0, 500'000);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, 0, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        auto const wantAddr1Code = makeDelegationIndicatorCode(aa);
        auto const addr1CodeEntry = co_await readAccountCode(storage, addr1Evmc, false);
        BOOST_REQUIRE(addr1CodeEntry);
        BOOST_CHECK_EQUAL(addr1CodeEntry->get().size(), wantAddr1Code.size());
        BOOST_CHECK(executor::isEip7702DelegationIndicator(bytesConstRef(
            reinterpret_cast<byte const*>(addr1CodeEntry->get().data()),
            addr1CodeEntry->get().size())));

        auto const wantAddr2Code = makeDelegationIndicatorCode(bb);
        auto const addr2CodeEntry = co_await readAccountCode(storage, addr2Evmc, false);
        BOOST_REQUIRE(addr2CodeEntry);
        BOOST_CHECK_EQUAL(addr2CodeEntry->get().size(), wantAddr2Code.size());
        BOOST_CHECK(executor::isEip7702DelegationIndicator(bytesConstRef(
            reinterpret_cast<byte const*>(addr2CodeEntry->get().data()),
            addr2CodeEntry->get().size())));

        EVMAccount<decltype(storage)> addr2Acct(storage, addr2Evmc, false);
        auto const slotKey = bytes32WithTrailingByte(0x42);
        auto const stored = co_await addr2Acct.storage(slotKey);
        BOOST_CHECK_EQUAL(stored.bytes[31], 0x42);
    }());
}

BOOST_AUTO_TEST_CASE(delegated_account_call_gas_near_geth_5455)
{
    syncWait([this]() -> task::Task<void> {
        // geth runtime_test.go TestDelegatedAccountAccessCost reports absolute CALL opcode cost
        // (~5455) via tracer; the marginal receipt delta here is cold delegation-target access
        // (evmone cold_account_access_cost = 2600) plus callee execution overhead.
        auto const authority = evmcAddr19(0xff);
        auto const target = address19(0xaa);
        auto const caller = address19(0xcc);

        {
            EVMAccount<decltype(storage)> targetAcct(storage, target, false);
            co_await targetAcct.create();
            auto const& retCode = returnEmptyBytecode();
            auto const codeHash =
                cryptoSuite->hashImpl()->hash(bytesConstRef(retCode.data(), retCode.size()));
            co_await targetAcct.setCode(retCode, std::string{}, codeHash);
        }
        {
            EVMAccount<decltype(storage)> authorityAcct(storage, authority, false);
            co_await authorityAcct.create();
            co_await authorityAcct.setNonce("0");
        }
        {
            EVMAccount<decltype(storage)> callerAcct(storage, caller, false);
            co_await callerAcct.create();
            auto const callCode = callDelegatedAccountBytecode(0xff);
            auto const codeHash =
                cryptoSuite->hashImpl()->hash(bytesConstRef(callCode.data(), callCode.size()));
            co_await callerAcct.setCode(callCode, std::string{}, codeHash);
        }

        evmc_address sender = evmcAddr19(0x02);
        co_await fundSender(sender);

        auto const runCallerTx = [&](uint64_t nonce) -> task::Task<uint64_t> {
            auto tx = makeWeb3Type2Transaction(sender, caller, bytes{}, 500'000, nonce);
            auto receipt = co_await executor.executeTransaction(
                storage, blockHeader, *tx, 0, ledgerConfig, false);
            BOOST_REQUIRE(receipt);
            BOOST_CHECK_EQUAL(receipt->status(), 0);
            co_return receipt->gasUsed().convert_to<uint64_t>();
        };

        auto const gasPlain = co_await runCallerTx(0);

        co_await setDelegationIndicator(
            storage, cryptoSuite->hashImpl(), authority, target, false);

        auto const gasDelegated = co_await runCallerTx(1);
        auto const delegatedCallOverhead = gasDelegated > gasPlain ? gasDelegated - gasPlain : 0;

        // evmone Prague: extra cold delegation-target access (2600) + minimal callee work.
        BOOST_CHECK_GT(delegatedCallOverhead, 0U);
        BOOST_CHECK_GE(delegatedCallOverhead, 2550U);
        BOOST_CHECK_LE(delegatedCallOverhead, 2650U);
    }());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
