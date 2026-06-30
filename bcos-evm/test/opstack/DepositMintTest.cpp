#define BOOST_TEST_MODULE DepositMintTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/InMemoryStateView.h"
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

OpStackExecutionRequest makeDepositInput(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    const crypto::Hash& hash, const evmc_address& sender, const evmc_address& recipient)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    OpStackExecutionRequest input;
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
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{.from = sender,
        .to = recipient,
        .mint = u256(100),
        .value = 0,
        .gas = static_cast<uint64_t>(message.gas)};
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(deposit_mint_is_applied_before_execution)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x31);
    auto const target = addressFromLastByte(0x32);

    state::Account senderAccount;
    senderAccount.nonce = 3;
    senderAccount.balance = 0;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeDepositInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, sender), u256(100));
    BOOST_CHECK_EQUAL(nonceFromDiff(output.stateDiff, sender, senderAccount.nonce), 4);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, 3);
}
}  // namespace bcos::evm::test
