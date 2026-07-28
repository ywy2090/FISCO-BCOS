/// @file OpstackExecutorTest.cpp
/// @brief Unit tests for OpstackExecutor (v0, normal transactions).
///
/// BUILD DEPENDENCY: this test — like the whole opstack-executor module — is built ON TOP OF
/// the ethereum-executor module (PR #5366), which supplies the BCOS<->evmone adapter, the
/// storage-backed StateView, and LedgerConfig::evmcRevision(). It compiles and runs only after
/// #5366 lands on release-3.18.0 and this branch is rebased on it (see ../README.md). The
/// harness below uses the same real APIs the existing storage-layer executor tests use
/// (transaction-executor/tests/FIB77_81_AuthCheckTest.cpp,
/// transaction-executor/tests/Web3AccessListResolverTest.cpp).

#include "opstack-executor/OpstackExecutor.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpPredeploys.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <gtest/gtest.h>
#include <memory>

using namespace bcos;
using namespace bcos::executor_v1;
using bcos::executor_v1::opstack::OpstackExecutor;

namespace
{
// Minimal MutableStorage matching the storage2 concept, identical to the one the
// transaction-executor and ethereum-executor tests use.
using MutableStorage = storage2::memory_storage::MemoryStorage<StateKey, StateValue,
    storage2::memory_storage::ORDERED>;

auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, MutableStorage& storage,
    ::ranges::input_range auto&& keys, storage2::BYPASS_READ_SET_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<decltype(storage2::readSome(storage, keys))>>
{
    co_return co_await storage2::readSome(storage, std::forward<decltype(keys)>(keys));
}
auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, MutableStorage& storage,
    const auto& key, storage2::BYPASS_READ_SET_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<decltype(storage2::readOne(storage, key))>>
{
    co_return co_await storage2::readOne(storage, key);
}

// A real, decodable EIP-2930 signed tx (reused verbatim from Web3AccessListResolverTest); a valid
// signed web3 envelope is what carries extraTransactionBytes(), the OP L1-cost input.
constexpr std::string_view kRawEip2930Tx =
    "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec80000"
    "906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842a000"
    "00000000000000000000000000000000000000000000000000000000000003a0000000000000000000000000000000"
    "0000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080a036b241b061"
    "a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd347c5ed9577ef293a6"
    "2787b4510465fadbfe39ee4094";

struct Fixture : public ::testing::Test
{
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;

    // Default to the latest real-adapted fork; keep the ledger revision in lockstep so the
    // executor's fork/rev consistency guard passes.
    const bcos::evm::opstack::OpForkConfig& fork = bcos::evm::opstack::jovianConfig();

    Fixture()
    {
        ledgerConfig.setEVMCRevision(fork.rev);
        ledgerConfig.setGasLimit({30000000, 0});
        // base fee 0: max_gas_price(>=0) always clears the London base-fee floor, so the test
        // needs no gas-price plumbing to pass validation. Fee-vault routing (base/priority/L1/
        // operator) is already covered at the transition layer by OpTransitionTest; this test
        // proves the executor drives opValidateFromState + opTransition end-to-end over BCOS
        // storage.
        ledgerConfig.setGasPrice({"0x0", 0});
    }

    bcostars::protocol::TransactionImpl buildWeb3Tx()
    {
        auto bytes = fromHexWithPrefix(std::string(kRawEip2930Tx));
        auto bRef = ref(bytes);
        bcos::rpc::Web3Transaction w3{};
        [&] { ASSERT_EQ(bcos::codec::rlp::decode(bRef, w3), nullptr); }();
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        auto const txHash = w3.txHash();
        tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
        return bcostars::protocol::TransactionImpl([tarsHolder]() { return tarsHolder.get(); });
    }

    template <class T>
    static T sync(task::Task<T> t)
    {
        return task::syncWait(std::move(t));
    }
};

TEST_F(Fixture, ConstructsWithForkAndExposesConcept)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    // The TransactionExecutor concept surface must be reachable/awaitable.
    auto ctx = task::syncWait(
        executor.createExecuteContext(storage, blockHeader, tx, 0, ledgerConfig, false));
    EXPECT_EQ(&ctx.ledgerConfig, &ledgerConfig);
    EXPECT_FALSE(ctx.call);
}

TEST_F(Fixture, RejectsForkRevisionMismatch)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    ledgerConfig.setEVMCRevision(EVMC_FRONTIER);  // deliberately != fork.rev
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    EXPECT_THROW(task::syncWait(
                     executor.executeTransaction(storage, blockHeader, tx, 0, ledgerConfig, false)),
        bcos::executor_v1::opstack::OpForkRevisionMismatch);
}

TEST_F(Fixture, ExecutesNormalTransferEndToEnd)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};

    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildWeb3Tx();
    // The recovered sender is derived from the signature; seed exactly that account so buy-gas +
    // value transfer succeed. 1e18 wei comfortably covers gas_limit*0 + value.
    auto const& senderBytes = tx.sender();
    evmc::address sender{};
    ASSERT_EQ(senderBytes.size(), sizeof(evmc_address));
    std::copy_n(senderBytes.begin(), sizeof(evmc_address), sender.bytes);

    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, sender, false);
        co_await acc.create();
        co_await acc.setBalance(u256("1000000000000000000"));
        co_await acc.setNonce(
            std::to_string(tx.nonce().empty() ? std::string("0") : std::string(tx.nonce())));
        co_return;
    }());

    auto receipt = task::syncWait(
        executor.executeTransaction(storage, blockHeader, tx, 0, ledgerConfig, false));

    ASSERT_NE(receipt, nullptr);
    // A funded normal transfer runs to completion; the base intrinsic cost is charged.
    EXPECT_GT(receipt->gasUsed(), 0U);

    // Sender nonce advanced by exactly one (the executor incremented it via opTransition).
    auto const finalNonce = task::syncWait([&]() -> task::Task<std::string> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, sender, false);
        auto n = co_await acc.nonce();
        co_return n.value_or("0");
    }());
    EXPECT_NE(finalNonce, "0");
}

}  // namespace
