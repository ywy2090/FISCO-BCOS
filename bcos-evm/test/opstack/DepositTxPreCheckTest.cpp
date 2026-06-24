#define BOOST_TEST_MODULE DepositTxPreCheckTest

#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackTxPrecheck.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "state/InMemoryEvmStateReader.h"
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
}  // namespace

BOOST_AUTO_TEST_CASE(system_deposit_is_rejected)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    state::State state(stateView);
    auto input = makeInput(sender);
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{.isSystemTransaction = true};

    auto error = opStackTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(deposit_skips_nonce_and_fee_checks_but_still_subtracts_gas_pool)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x02);
    state::Account account;
    account.nonce = 99;
    account.balance = 0;
    stateView.insert_account(sender, account);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{};
    input.nonce = 1;
    input.gasTipCap = 100;
    input.gasFeeCap = 1;
    bool hookCalled = false;
    input.gasPoolSubGasHook = [&](uint64_t gas) {
        hookCalled = true;
        return gas == static_cast<uint64_t>(input.message.gas);
    };

    auto error = opStackTxPrecheck(input, state);
    BOOST_CHECK(!error.has_value());
    BOOST_CHECK(hookCalled);
}

BOOST_AUTO_TEST_CASE(non_deposit_rejects_nonce_mismatch)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x03);
    state::Account account;
    account.nonce = 5;
    stateView.insert_account(sender, account);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 7;

    auto error = opStackTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(non_deposit_rejects_invalid_eip1559_caps)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x04);
    state::Account account;
    account.nonce = 1;
    stateView.insert_account(sender, account);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.gasTipCap = 3;
    input.gasFeeCap = 2;

    auto error = opStackTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(non_deposit_rejects_blob_fee_cap_under_base_fee)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x05);
    state::Account account;
    account.nonce = 1;
    stateView.insert_account(sender, account);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = true;
    h256 validHash{};
    validHash[0] = 0x01;
    input.blobVersionedHashes.push_back(validHash);
    input.blobGasFeeCap = 2;

    auto error = opStackTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(non_deposit_rejects_blob_fields_when_eip4844_disabled)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x07);
    state::Account account;
    account.nonce = 1;
    stateView.insert_account(sender, account);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.nonce = 1;
    input.revisionConfig.eip4844 = false;
    input.blobVersionedHashes.push_back(bcos::h256(1));
    input.blobGasFeeCap = 100;

    auto error = opStackTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(non_deposit_rejects_auth_list_on_create)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x06);
    state::Account account;
    account.nonce = 0;
    stateView.insert_account(sender, account);

    state::State state(stateView);
    auto input = makeInput(sender);
    input.message.kind = EVMC_CREATE;
    input.nonce = 0;
    input.authorizations.push_back({});

    auto error = opStackTxPrecheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}
}  // namespace bcos::evm::test
