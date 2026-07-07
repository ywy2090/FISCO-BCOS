#define BOOST_TEST_MODULE EthExecuteViaEthPreCheckTest

#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/eip/Eip3860.h"
#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/eip/Eip7825.h"
#include "helpers/EthPreCheckRulesTestHelper.h"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <limits>

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

EthMessageRequest makeInput(evmc_address sender)
{
    EthMessageRequest input;
    input.message.kind = EVMC_CALL;
    input.message.sender = sender;
    input.message.gas = 50'000;
    input.blockInfo.baseFee = 1;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    input.hasExplicitFeeCaps = true;
    input.revisionConfig.eip1559 = true;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(rejects_sender_with_non_delegation_code)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x10);

    state::Account senderAccount;
    senderAccount.code = {0x00};
    stateView.insert_account(sender, std::move(senderAccount));

    auto input = makeInput(sender);
    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(allows_sender_with_delegation_code)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x11);
    auto const target = addressFromLastByte(0x22);

    state::Account senderAccount;
    senderAccount.code = addressToDelegation(target);
    stateView.insert_account(sender, std::move(senderAccount));

    auto input = makeInput(sender);
    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(rejects_explicit_empty_authorization_list)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x12);

    auto input = makeInput(sender);
    input.authorizationListPresent = true;
    input.web3TypedTxKind = 0x04;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_type4_contract_creation)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x13);

    auto input = makeInput(sender);
    input.message.kind = EVMC_CREATE;
    input.web3TypedTxKind = 0x04;
    input.authorizations.push_back({});

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_priority_fee_above_max_fee)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x14);

    auto input = makeInput(sender);
    input.gasTipCap = 3;
    input.gasFeeCap = 2;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_max_fee_below_base_fee)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x15);

    auto input = makeInput(sender);
    input.blockInfo.baseFee = 10;
    input.gasTipCap = 1;
    input.gasFeeCap = 5;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_legacy_gas_price_below_base_fee)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x1f);

    auto input = makeInput(sender);
    input.hasExplicitFeeCaps = false;
    input.web3TypedTxKind = 0;
    input.gasPrice = 999;
    input.gasTipCap = 999;
    input.gasFeeCap = 999;
    input.blockInfo.baseFee = 1000;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_tx_gas_limit_above_block_gas_limit)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x20);

    auto input = makeInput(sender);
    input.blockInfo.gasLimit = 80'000;
    input.message.gas = 90'000;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::OutOfGasLimit);
}

BOOST_AUTO_TEST_CASE(allows_tx_gas_limit_at_block_gas_limit)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x21);

    auto input = makeInput(sender);
    input.blockInfo.gasLimit = 80'000;
    input.message.gas = 80'000;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(rejects_tx_nonce_at_uint64_max)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x16);

    auto input = makeInput(sender);
    input.txNonce = std::numeric_limits<uint64_t>::max();

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(rejects_oversized_initcode_on_shanghai)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x17);

    bytes initcode(MAX_INIT_CODE_SIZE + 1, 0x00);
    auto input = makeInput(sender);
    input.message.kind = EVMC_CREATE;
    input.message.input_data = initcode.data();
    input.message.input_size = initcode.size();
    input.revisionConfig.revision = EVMC_SHANGHAI;
    input.revisionConfig.eip3860 = true;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(allows_max_initcode_size_on_shanghai)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x18);

    bytes initcode(MAX_INIT_CODE_SIZE, 0x00);
    auto input = makeInput(sender);
    input.message.kind = EVMC_CREATE;
    input.message.input_data = initcode.data();
    input.message.input_size = initcode.size();
    input.revisionConfig.revision = EVMC_SHANGHAI;
    input.revisionConfig.eip3860 = true;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(rejects_tx_gas_limit_above_osaka_cap)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x19);

    auto input = makeInput(sender);
    input.revisionConfig.revision = EVMC_OSAKA;
    input.revisionConfig.eip7825 = true;
    input.message.gas = MAX_TX_GAS + 1;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::OutOfGasLimit);
}

BOOST_AUTO_TEST_CASE(allows_tx_gas_limit_at_osaka_cap)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x1a);

    auto input = makeInput(sender);
    input.revisionConfig.revision = EVMC_OSAKA;
    input.revisionConfig.eip7825 = true;
    input.message.gas = MAX_TX_GAS;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(allows_tx_gas_above_cap_when_eip7825_off)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x1b);

    auto input = makeInput(sender);
    input.revisionConfig.revision = EVMC_OSAKA;
    input.revisionConfig.eip7825 = false;
    input.message.gas = MAX_TX_GAS + 1;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(allows_tx_gas_above_cap_before_osaka)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x1c);

    auto input = makeInput(sender);
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.eip7825 = false;
    input.message.gas = MAX_TX_GAS + 1;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(rejects_type03_with_zero_blobs)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x1d);

    auto input = makeInput(sender);
    input.revisionConfig.eip4844 = true;
    input.web3TypedTxKind = 0x03;
    input.blobGasFeeCap = 1;

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_blob_count_above_cancun_cap)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x1e);

    auto input = makeInput(sender);
    input.revisionConfig.eip4844 = true;
    input.web3TypedTxKind = 0x03;
    input.blobGasFeeCap = 1;
    input.blobVersionedHashes.assign(gas::MAX_BLOBS_PER_TX + 1, h256{0x01});

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_insufficient_max_fee_per_blob_gas)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x1f);

    auto input = makeInput(sender);
    input.revisionConfig.eip4844 = true;
    input.web3TypedTxKind = 0x03;
    input.blockInfo.blobBaseFee = 2;
    input.blobGasFeeCap = 1;
    h256 blobHash{};
    blobHash[0] = 0x01;
    input.blobVersionedHashes = {blobHash};

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
    BOOST_CHECK_EQUAL(error->status_code, EVMC_INSUFFICIENT_BALANCE);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_blob_versioned_hash_prefix)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x20);

    auto input = makeInput(sender);
    input.revisionConfig.eip4844 = true;
    input.web3TypedTxKind = 0x03;
    input.blobGasFeeCap = 1;
    input.blockInfo.blobBaseFee = 1;
    h256 invalidVersionHash{};
    invalidVersionHash[0] = 0x02;
    input.blobVersionedHashes = {invalidVersionHash};

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}
}  // namespace bcos::evm::test
