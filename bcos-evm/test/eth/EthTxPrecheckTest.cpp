#define BOOST_TEST_MODULE EthExecuteViaEthPreCheckTest

#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/eip/Eip3860.h"
#include "bcos-evm/eth/eip/Eip7702.h"
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

    auto error = ethPreCheckRulesError(input, stateView);
    BOOST_CHECK(!error.has_value());
}
}  // namespace bcos::evm::test
