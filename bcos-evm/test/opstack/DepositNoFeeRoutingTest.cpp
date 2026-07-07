#define BOOST_TEST_MODULE DepositNoFeeRoutingTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::test
{
namespace
{
class FakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef /*unused*/) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

u256 balanceFromDiff(const state::StateDiff& diff, const evmc_address& address)
{
    auto const it = diff.accounts.find(address);
    if (it == diff.accounts.end())
    {
        return 0;
    }
    return it->second.balance;
}

uint64_t nonceFromDiff(
    const state::StateDiff& diff, const evmc_address& address, uint64_t fallbackNonce)
{
    auto const it = diff.accounts.find(address);
    if (it == diff.accounts.end())
    {
        return fallbackNonce;
    }
    return it->second.nonce;
}

OpStackMessageRequest makeDepositInput(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    const crypto::Hash& hash, const evmc_address& sender, const evmc_address& recipient)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    OpStackMessageRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.blockInfo.number = 1;
    input.blockInfo.timestamp = 12345;
    input.blockInfo.gasLimit = 30'000'000;
    input.blockInfo.baseFee = 1;
    input.blockInfo.coinbase = addressFromLastByte(0x99);
    input.revisionConfig.revision = EVMC_CANCUN;
    input.web3TypedTxKind = toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit);
    input.depositTx = OpStackDepositTx{
        .from = sender, .to = recipient, .mint = u256(100), .value = 0, .gas = 50'000};
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(deposit_skips_fee_routing_recipients)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x41);
    auto const target = addressFromLastByte(0x42);
    auto const coinbase = addressFromLastByte(0x99);

    state::Account senderAccount;
    senderAccount.balance = 0;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeDepositInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, OP_BASE_FEE_RECIPIENT), u256(0));
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, OP_L1_FEE_RECIPIENT), u256(0));
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, OP_OPERATOR_FEE_RECIPIENT), u256(0));
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, coinbase), u256(0));
}

BOOST_AUTO_TEST_CASE(deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x51);
    auto const target = addressFromLastByte(0x52);

    state::Account senderAccount;
    senderAccount.nonce = 7;
    senderAccount.balance = 0;
    stateView.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = {0x60, 0x00, 0x60, 0x00, 0xfd};  // PUSH1 0 PUSH1 0 REVERT
    stateView.insert_account(target, targetAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeDepositInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(output.gasUsed, 21'006);
    BOOST_CHECK_LT(output.gasUsed, 50'000);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, sender), u256(100));
    BOOST_CHECK_EQUAL(nonceFromDiff(output.stateDiff, sender, senderAccount.nonce), 8);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, 7);
    BOOST_REQUIRE(output.receiptMeta.depositReceiptVersion.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositReceiptVersion, 1u);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, OP_BASE_FEE_RECIPIENT), u256(0));
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, OP_L1_FEE_RECIPIENT), u256(0));
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, OP_OPERATOR_FEE_RECIPIENT), u256(0));
}

BOOST_AUTO_TEST_CASE(deposit_entry_failure_bumps_nonce_and_uses_gas_limit)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x61);
    auto const target = addressFromLastByte(0x62);

    state::Account senderAccount;
    senderAccount.nonce = 5;
    senderAccount.balance = 0;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeDepositInput(stateView, vm, hash, sender, target);
    input.message.gas = 20'999;
    input.depositTx->gas = 20'999;
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.gasUsed, 20'999);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, sender), u256(100));
    BOOST_CHECK_EQUAL(nonceFromDiff(output.stateDiff, sender, senderAccount.nonce), 6);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, 5);
    BOOST_REQUIRE(output.receiptMeta.depositReceiptVersion.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositReceiptVersion, 1u);
}
}  // namespace bcos::evm::test
