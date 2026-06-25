#define BOOST_TEST_MODULE OpStackPreCheck4844Test

#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "helpers/InMemoryEvmStateReader.h"
#include "helpers/OpStackEntryPrecheck.h"
#include <boost/test/included/unit_test.hpp>

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

h256 makeVersionedHash(uint8_t versionByte)
{
    h256 hash{};
    hash[0] = versionByte;
    hash[31] = 0x42;
    return hash;
}

OpStackExecutionRequest makeInput(const evmc_address& sender)
{
    OpStackExecutionRequest input;
    input.message.kind = EVMC_CALL;
    input.message.sender = sender;
    input.message.gas = 30'000;
    input.blockInfo.baseFee = 2;
    input.blockInfo.blobBaseFee = 3;
    input.gasTipCap = 2;
    input.gasFeeCap = 2;
    return input;
}

void setupAccountWithNonce(
    state::test::InMemoryEvmStateReader& stateView, const evmc_address& sender, uint64_t nonce)
{
    state::Account account;
    account.nonce = nonce;
    stateView.insert_account(sender, account);
}
}  // namespace

BOOST_AUTO_TEST_CASE(rejects_blob_create)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    setupAccountWithNonce(stateView, sender, 1);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = true;
    input.message.kind = EVMC_CREATE;
    input.blobVersionedHashes.push_back(makeVersionedHash(0x01));
    input.blobGasFeeCap = 10;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_type03_with_empty_hashes)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x02);
    setupAccountWithNonce(stateView, sender, 1);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = true;
    input.web3TypedTxKind = 0x03;
    input.blobGasFeeCap = 10;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_versioned_hash_prefix)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x03);
    setupAccountWithNonce(stateView, sender, 1);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = true;
    input.blobVersionedHashes.push_back(makeVersionedHash(0x02));
    input.blobGasFeeCap = 10;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_blob_when_eip4844_disabled)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x04);
    setupAccountWithNonce(stateView, sender, 1);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = false;
    input.blobVersionedHashes.push_back(makeVersionedHash(0x01));
    input.blobGasFeeCap = 10;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_low_blob_fee_cap)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x05);
    setupAccountWithNonce(stateView, sender, 1);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = true;
    input.blobVersionedHashes.push_back(makeVersionedHash(0x01));
    input.blobGasFeeCap = 2;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(accepts_valid_blob_precheck)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x06);
    setupAccountWithNonce(stateView, sender, 1);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = true;
    input.web3TypedTxKind = 0x03;
    input.blobVersionedHashes.push_back(makeVersionedHash(0x01));
    input.blobGasFeeCap = 10;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_CHECK(!error.has_value());
}
}  // namespace bcos::evm::test
