#define BOOST_TEST_MODULE DepositCreateNonceTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "state/InMemoryEvmStateReader.h"
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

// Minimal init code: RETURN empty runtime bytecode (PUSH1 0 PUSH1 0 RETURN).
bcos::bytes minimalCreateInitCode()
{
    return {0x60, 0x00, 0x60, 0x00, 0xf3};
}

OpStackExecutionRequest makeCreateDepositInput(state::test::InMemoryEvmStateReader& stateView,
    evmc::VM& vm, const crypto::Hash& hash, const evmc_address& sender, bcos::bytes const& initCode)
{
    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.gas = 100'000;
    message.sender = sender;
    message.recipient = {};
    message.code_address = {};
    message.input_data = initCode.data();
    message.input_size = initCode.size();

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
        .to = std::nullopt,
        .value = 0,
        .gas = static_cast<uint64_t>(message.gas),
        .data = initCode};
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(create_deposit_bumps_sender_nonce_exactly_once)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x41);
    auto const initialNonce = uint64_t{3};

    state::Account senderAccount;
    senderAccount.nonce = initialNonce;
    senderAccount.balance = 0;
    stateView.insert_account(sender, senderAccount);

    auto const initCode = minimalCreateInitCode();

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeCreateDepositInput(stateView, vm, hash, sender, initCode);
    auto output = task::syncWait(opStackExecute(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(
        nonceFromDiff(output.stateDiff, sender, senderAccount.nonce), initialNonce + 1);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, initialNonce);
}
}  // namespace bcos::evm::test
