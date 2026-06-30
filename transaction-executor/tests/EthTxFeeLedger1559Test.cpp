/*
 * End-to-end transaction-executor tests for EIP-1559 TE gas settlement.
 */
#include "../bcos-transaction-executor/EthTransactionExecutorImpl.h"
#include "TestMemoryStorage.h"
#include "bcos-evm/eth/eip/TxIntrinsicGas.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <intx/intx.hpp>

using namespace bcos;
using namespace bcos::executor_v1;
using namespace bcos::storage2;

namespace bcos::test
{
namespace
{
constexpr uint64_t BASE_FEE = 10;
constexpr uint64_t MAX_PRIORITY_FEE_PER_GAS = 2;
constexpr uint64_t MAX_FEE_PER_GAS = 100;
constexpr uint64_t EFFECTIVE_GAS_PRICE = BASE_FEE + MAX_PRIORITY_FEE_PER_GAS;

void setPragueFeatures(ledger::LedgerConfig& ledgerConfig)
{
    ledger::Features features;
    features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_evm_prague);
    features.set(ledger::Features::Flag::feature_evm_eip2929);
    features.set(ledger::Features::Flag::feature_balance);
    features.set(ledger::Features::Flag::feature_balance_policy1);
    ledgerConfig.setFeatures(features);
}

void setLedgerChainId(ledger::LedgerConfig& ledgerConfig, uint64_t chainId = 1)
{
    evmc_uint256be evmcChain{};
    intx::be::store(evmcChain.bytes, intx::uint256{chainId});
    ledgerConfig.setChainId(evmcChain);
}

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

Address toBcosAddress(evmc_address const& address)
{
    Address out{};
    std::memcpy(out.data(), address.bytes, sizeof(address.bytes));
    return out;
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeWeb3Type2Transaction(
    evmc_address const& sender, bcos::Address const& to, bcos::bytes const& data, uint64_t gasLimit,
    u256 value = 0)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 1;
    w3.nonce = 0;
    w3.maxPriorityFeePerGas = MAX_PRIORITY_FEE_PER_GAS;
    w3.maxFeePerGas = MAX_FEE_PER_GAS;
    w3.gasLimit = gasLimit;
    w3.to = to;
    w3.value = value;
    w3.data = data;
    w3.signatureR = bcos::bytes(32, 0x11);
    w3.signatureS = bcos::bytes(32, 0x22);
    w3.signatureV = 27;

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    auto const txHash = w3.hashForSign();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->sender.assign(sender.bytes, sender.bytes + sizeof(sender.bytes));

    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
}
}  // namespace

class EthTxFeeSettlement1559Fixture
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    executor_v1::EthTransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl()};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    evmc_address sender = addressFromLastByte(0x51);
    evmc_address target = addressFromLastByte(0x52);
    evmc_address coinbase = addressFromLastByte(0xcb);
    int contextId = 0;

    EthTxFeeSettlement1559Fixture()
    {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
        setPragueFeatures(ledgerConfig);
        setLedgerChainId(ledgerConfig, 1);
        ledgerConfig.setGasLimit({30'000'000, 0});
        ledgerConfig.setGasPrice({std::to_string(BASE_FEE), 0});
        ledgerConfig.setBalanceTransfer(true);

        blockHeader.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        blockHeader.setNumber(1);
        blockHeader.setTimestamp(12'345);
        blockHeader.setExtraData(bytes(coinbase.bytes, coinbase.bytes + sizeof(coinbase.bytes)));
        blockHeader.calculateHash(*cryptoSuite->hashImpl());
    }

    task::Task<void> ensureAccount(evmc_address const& address, u256 balance = 0)
    {
        ledger::account::EVMAccount<decltype(storage)> account(storage, address, false);
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        co_await account.setBalance(balance);
    }

    task::Task<void> fundSender(u256 balance = u256(1) << 96)
    {
        ledger::account::EVMAccount<decltype(storage)> account(storage, sender, false);
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        co_await account.setBalance(balance);
        co_await account.setNonce("0");
    }

    task::Task<void> deployMeteredStopAt(evmc_address const& address)
    {
        ledger::account::EVMAccount<decltype(storage)> account(storage, address, false);
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        bytes const code{0x60, 0x00, 0x50, 0x00};
        auto const codeHash = cryptoSuite->hashImpl()->hash(ref(code));
        co_await account.setCode(code, "", codeHash);
    }

    task::Task<u256> balanceOf(evmc_address const& address)
    {
        ledger::account::EVMAccount<decltype(storage)> account(storage, address, false);
        co_return co_await account.balance();
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeStopCall(uint64_t gasLimit = 500'000)
    {
        co_await deployMeteredStopAt(target);
        auto tx = makeWeb3Type2Transaction(sender, toBcosAddress(target), bytes{}, gasLimit);
        co_return co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);
    }
};

BOOST_FIXTURE_TEST_SUITE(EthTxFeeSettlement1559, EthTxFeeSettlement1559Fixture)

BOOST_AUTO_TEST_CASE(buy_gas_debits_effective_times_limit)
{
    task::syncWait([this]() -> task::Task<void> {
        u256 const initialBalance = u256(1) << 96;
        co_await fundSender(initialBalance);
        co_await ensureAccount(coinbase);

        auto receipt = co_await executeStopCall();

        BOOST_REQUIRE(receipt);
        BOOST_REQUIRE_EQUAL(receipt->status(), 0);
        auto const senderBalance = co_await balanceOf(sender);
        BOOST_CHECK_EQUAL(
            initialBalance - senderBalance, u256(receipt->gasUsed()) * EFFECTIVE_GAS_PRICE);
    }());
}

BOOST_AUTO_TEST_CASE(coinbase_receives_tip_only)
{
    task::syncWait([this]() -> task::Task<void> {
        co_await fundSender();
        co_await ensureAccount(coinbase);

        auto receipt = co_await executeStopCall();

        BOOST_REQUIRE(receipt);
        BOOST_REQUIRE_EQUAL(receipt->status(), 0);
        auto const coinbaseBalance = co_await balanceOf(coinbase);
        BOOST_CHECK_EQUAL(coinbaseBalance, u256(receipt->gasUsed()) * MAX_PRIORITY_FEE_PER_GAS);
        BOOST_CHECK_NE(coinbaseBalance, u256(receipt->gasUsed()) * EFFECTIVE_GAS_PRICE);
    }());
}

BOOST_AUTO_TEST_CASE(burn_identity)
{
    task::syncWait([this]() -> task::Task<void> {
        u256 const initialBalance = u256(1) << 96;
        co_await fundSender(initialBalance);
        co_await ensureAccount(coinbase);

        auto receipt = co_await executeStopCall();

        BOOST_REQUIRE(receipt);
        BOOST_REQUIRE_EQUAL(receipt->status(), 0);
        auto const senderBalance = co_await balanceOf(sender);
        auto const coinbaseBalance = co_await balanceOf(coinbase);
        auto const senderDebit = initialBalance - senderBalance;
        BOOST_REQUIRE_GE(senderDebit, coinbaseBalance);
        BOOST_CHECK_EQUAL(senderDebit - coinbaseBalance, u256(receipt->gasUsed()) * BASE_FEE);
    }());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
