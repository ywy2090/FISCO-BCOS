#define BOOST_TEST_MODULE OpStackExecuteViaHostSmokeTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

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

OpStackExecuteViaHostInput makeBaseInput(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    const crypto::Hash& hash, const evmc_address& sender, const evmc_address& recipient)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    OpStackExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.gasPrice = 2;
    input.blockInfo.number = 1;
    input.blockInfo.timestamp = 12345;
    input.blockInfo.gasLimit = 30'000'000;
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{.cachedData = {0x01, 0x02}};
    input.opTxExecutor.m_l1CostFunc = [](const RollupCostData&, uint64_t) { return u256(50); };
    input.opTxExecutor.m_l1FeeRecipient = addressFromLastByte(0xf1);
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(l1_fee_recipient_gets_fee_on_success)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    auto const l1FeeRecipient = addressFromLastByte(0xf1);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecuteViaHost(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(50));
    auto const expectedSenderBalance = u256(300'000) - u256(50) - u256(output.gasUsed) * u256(2);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, sender), expectedSenderBalance);
}

BOOST_AUTO_TEST_CASE(insufficient_balance_fails_before_execution)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x11);
    auto const target = addressFromLastByte(0x12);
    auto const l1FeeRecipient = addressFromLastByte(0xf1);

    state::Account senderAccount;
    senderAccount.balance = 100;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecuteViaHost(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(0));
}

BOOST_AUTO_TEST_CASE(revert_refunds_unused_gas_and_keeps_l1_fee)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x21);
    auto const target = addressFromLastByte(0x22);
    auto const l1FeeRecipient = addressFromLastByte(0xf1);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = {0x60, 0x00, 0x60, 0x00, 0xfd};  // PUSH1 0 PUSH1 0 REVERT
    stateView.insert_account(target, targetAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecuteViaHost(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_GT(output.gasUsed, 0);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(50));

    auto const expectedSenderBalance = u256(300'000) - u256(50) - u256(output.gasUsed) * u256(2);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, sender), expectedSenderBalance);
}
}  // namespace bcos::evm::test
