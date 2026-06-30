#define BOOST_TEST_MODULE EthExecuteViaEthPreCheckTest

#include "bcos-evm/eth/reference/EthTxPrecheck.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/reference/EthReferenceExecute.h"
#include "helpers/InMemoryEvmStateReader.h"
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

EthReferenceRequest makeInput(evmc_address sender)
{
    EthReferenceRequest input;
    input.message.kind = EVMC_CALL;
    input.message.sender = sender;
    input.message.gas = 50'000;
    input.blockInfo.baseFee = 1;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(rejects_sender_with_non_delegation_code)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x10);

    state::Account senderAccount;
    senderAccount.code = {0x00};
    stateView.insert_account(sender, std::move(senderAccount));
    state::State state(stateView);

    auto input = makeInput(sender);
    auto error = ethTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(allows_sender_with_delegation_code)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x11);
    auto const target = addressFromLastByte(0x22);

    state::Account senderAccount;
    senderAccount.code = addressToDelegation(target);
    stateView.insert_account(sender, std::move(senderAccount));
    state::State state(stateView);

    auto input = makeInput(sender);
    auto error = ethTxPrecheck(input, state);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(rejects_explicit_empty_authorization_list)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x12);
    state::State state(stateView);

    auto input = makeInput(sender);
    input.authorizationListPresent = true;
    input.web3TypedTxKind = 0x04;

    auto error = ethTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_type4_contract_creation)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x13);
    state::State state(stateView);

    auto input = makeInput(sender);
    input.message.kind = EVMC_CREATE;
    input.web3TypedTxKind = 0x04;
    input.authorizations.push_back({});

    auto error = ethTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_priority_fee_above_max_fee)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x14);
    state::State state(stateView);

    auto input = makeInput(sender);
    input.gasTipCap = 3;
    input.gasFeeCap = 2;

    auto error = ethTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_max_fee_below_base_fee)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x15);
    state::State state(stateView);

    auto input = makeInput(sender);
    input.blockInfo.baseFee = 10;
    input.gasTipCap = 1;
    input.gasFeeCap = 5;

    auto error = ethTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_tx_nonce_at_uint64_max)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x16);
    state::State state(stateView);

    auto input = makeInput(sender);
    input.txNonce = std::numeric_limits<uint64_t>::max();

    auto error = ethTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::NonceCheckFail);
}
}  // namespace bcos::evm::test
