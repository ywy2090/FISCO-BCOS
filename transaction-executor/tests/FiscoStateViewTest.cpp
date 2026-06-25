#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "TestMemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/FiscoBlockInfo.h"
#include "bcos-evm/bcos/FiscoEvmStateReader.h"
#include "bcos-evm/bcos/StateDiffApplier.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_bytes32 bytes32FromLastByte(uint8_t value)
{
    evmc_bytes32 data{};
    data.bytes[31] = value;
    return data;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FiscoEvmStateReaderTest)

BOOST_AUTO_TEST_CASE(read_account_from_storage)
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    executor_v1::MutableStorage storage;
    bcos::evm::Rollbackable<executor_v1::MutableStorage> rollbackableStorage(storage);

    auto const address = addressFromLastByte(0x11);
    ledger::account::EVMAccount account(rollbackableStorage, address, false);

    task::syncWait(account.create());
    task::syncWait(account.setBalance(1234));
    task::syncWait(account.setNonce("7"));

    bcos::bytes code{0x60, 0x01, 0x60, 0x00, 0x55};
    auto const codeHash = hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
    task::syncWait(account.setCode(code, "abi()", codeHash));

    auto const slot = bytes32FromLastByte(0x01);
    auto const value = bytes32FromLastByte(0x09);
    task::syncWait(account.setStorage(slot, value));

    state::FiscoEvmStateReader view(rollbackableStorage, false, *hashImpl);
    auto loaded = view.get_account(address);

    BOOST_REQUIRE(loaded.has_value());
    BOOST_CHECK_EQUAL(loaded->balance, bcos::u256(1234));
    BOOST_CHECK_EQUAL(loaded->nonce, 7);
    BOOST_CHECK_EQUAL(loaded->code, code);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        loaded->codeHash.bytes, loaded->codeHash.bytes + 32, codeHash.begin(), codeHash.end());
    auto storageValue = view.get_storage(address, slot);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        storageValue.bytes, storageValue.bytes + 32, value.bytes, value.bytes + 32);
}

BOOST_AUTO_TEST_CASE(apply_state_diff_to_storage)
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    executor_v1::MutableStorage storage;
    bcos::evm::Rollbackable<executor_v1::MutableStorage> rollbackableStorage(storage);

    auto const address = addressFromLastByte(0x22);
    auto const slot = bytes32FromLastByte(0x02);
    auto const value = bytes32FromLastByte(0x0a);

    state::StateDiff diff;
    auto& account = diff.accounts[address];
    account.balance = 9999;
    account.balanceDirty = true;
    account.nonce = 42;
    account.nonceDirty = true;
    account.code = {0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};
    account.codeDirty = true;
    account.storage[slot] = value;

    constexpr std::string_view kDefaultAbi = "set(uint)";
    task::syncWait(state::applyStateDiff(rollbackableStorage, diff, false, *hashImpl, kDefaultAbi));

    ledger::account::EVMAccount storedAccount(rollbackableStorage, address, false);
    auto storedBalance = task::syncWait(storedAccount.balance());
    auto storedNonce = task::syncWait(storedAccount.nonce());
    auto storedCode = task::syncWait(storedAccount.code());
    auto storedAbi = task::syncWait(storedAccount.abi());
    auto storedStorage = task::syncWait(storedAccount.storage(slot));

    BOOST_CHECK_EQUAL(storedBalance, bcos::u256(9999));
    BOOST_REQUIRE(storedNonce.has_value());
    BOOST_CHECK_EQUAL(*storedNonce, "42");
    BOOST_REQUIRE(storedCode.has_value());
    BOOST_CHECK_EQUAL(storedCode->get(), std::string(account.code.begin(), account.code.end()));
    BOOST_REQUIRE(storedAbi.has_value());
    BOOST_CHECK_EQUAL(storedAbi->get(), kDefaultAbi);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        storedStorage.bytes, storedStorage.bytes + 32, value.bytes, value.bytes + 32);
}

BOOST_AUTO_TEST_CASE(build_block_info_and_hash_reader)
{
    executor_v1::MutableStorage storage;
    bcos::evm::Rollbackable<executor_v1::MutableStorage> rollbackableStorage(storage);

    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(100);
    blockHeader.setTimestamp(123456);
    blockHeader.setExtraData(bcos::bytes(20, 0x2a));

    ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setGasLimit({30'000'000, 0});
    ledgerConfig.setChainId(toEvmC(bcos::u256(2025)));

    auto blockInfo = state::buildFiscoBlockInfo(blockHeader, ledgerConfig);
    BOOST_CHECK_EQUAL(blockInfo.number, 100);
    BOOST_CHECK_EQUAL(blockInfo.timestamp, 123456);
    BOOST_CHECK_EQUAL(blockInfo.gasLimit, 30'000'000);
    BOOST_CHECK_EQUAL(blockInfo.chainId, bcos::u256(2025));
    BOOST_CHECK_EQUAL_COLLECTIONS(blockInfo.coinbase.bytes, blockInfo.coinbase.bytes + 20,
        blockHeader.extraData().begin(), blockHeader.extraData().begin() + 20);

    bcos::crypto::HashType hash("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    task::syncWait(
        storage2::writeOne(storage, executor_v1::StateKey{ledger::SYS_NUMBER_2_HASH, "99"},
            storage::Entry(bcos::concepts::bytebuffer::toView(hash))));

    auto blockHashes = state::buildFiscoBlockHashes(rollbackableStorage, blockHeader.number());
    auto loadedHash = blockHashes(99);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        loadedHash.bytes, loadedHash.bytes + 32, hash.begin(), hash.end());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
