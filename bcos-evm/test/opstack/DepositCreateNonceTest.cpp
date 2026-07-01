#define BOOST_TEST_MODULE DepositCreateNonceTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/opstack/ApplyOpStackMessage.h"
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

// Init code that always REVERTs (PUSH1 0 PUSH1 0 REVERT).
bcos::bytes revertCreateInitCode()
{
    return {0x60, 0x00, 0x60, 0x00, 0xfd};
}

// Init code that loops until OOG (JUMPDEST; PUSH1 0; PUSH1 0; JUMP).
bcos::bytes infiniteLoopCreateInitCode()
{
    return {0x5b, 0x60, 0x00, 0x60, 0x00, 0x56};
}

int64_t createDepositIntrinsicDebit(bcos::bytes const& initCode)
{
    evmc_message probe{};
    probe.kind = EVMC_CREATE;
    probe.input_data = initCode.data();
    probe.input_size = initCode.size();
    auto const intrinsic = gas::computeTxIntrinsicGas(probe, {}, bcos::executor::DEPOSIT_TX_TYPE);
    return intrinsic.preExecutionDebit() + gas::calcAuthTupleIntrinsicGas(0);
}

uint64_t createDepositGasBelowIntrinsic(bcos::bytes const& initCode)
{
    return static_cast<uint64_t>(std::max<int64_t>(0, createDepositIntrinsicDebit(initCode) - 1));
}

uint64_t createDepositGasWithExecutionBudget(bcos::bytes const& initCode, int64_t executionBudget)
{
    return static_cast<uint64_t>(createDepositIntrinsicDebit(initCode) + executionBudget);
}

OpStackMessageRequest makeCreateDepositInput(state::test::InMemoryStateView& stateView,
    evmc::VM& vm, const crypto::Hash& hash, const evmc_address& sender, bcos::bytes const& initCode,
    uint64_t gasLimit = 100'000)
{
    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.gas = static_cast<int64_t>(gasLimit);
    message.sender = sender;
    message.recipient = {};
    message.code_address = {};
    message.input_data = initCode.data();
    message.input_size = initCode.size();

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
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{
        .from = sender, .to = std::nullopt, .value = 0, .gas = gasLimit, .data = initCode};
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(create_deposit_bumps_sender_nonce_exactly_once)
{
    state::test::InMemoryStateView stateView;
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
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(
        nonceFromDiff(output.stateDiff, sender, senderAccount.nonce), initialNonce + 1);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, initialNonce);
}

BOOST_AUTO_TEST_CASE(create_deposit_revert_bumps_sender_nonce_exactly_once)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x42);
    auto const initialNonce = uint64_t{7};
    auto const gasLimit = uint64_t{100'000};

    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = initialNonce});

    auto const initCode = revertCreateInitCode();

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeCreateDepositInput(stateView, vm, hash, sender, initCode, gasLimit);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_LT(output.gasUsed, static_cast<int64_t>(gasLimit));
    BOOST_CHECK_EQUAL(nonceFromDiff(output.stateDiff, sender, initialNonce), initialNonce + 1);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, initialNonce);
}

BOOST_AUTO_TEST_CASE(create_deposit_intrinsic_reject_bumps_sender_nonce_exactly_once)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x43);
    auto const initialNonce = uint64_t{5};

    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = initialNonce});

    auto const initCode = minimalCreateInitCode();
    auto const gasLimit = createDepositGasBelowIntrinsic(initCode);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeCreateDepositInput(stateView, vm, hash, sender, initCode, gasLimit);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.gasUsed, static_cast<int64_t>(gasLimit));
    BOOST_CHECK_EQUAL(nonceFromDiff(output.stateDiff, sender, initialNonce), initialNonce + 1);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, initialNonce);
}

BOOST_AUTO_TEST_CASE(create_deposit_oog_bumps_sender_nonce_exactly_once)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x44);
    auto const initialNonce = uint64_t{9};

    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = initialNonce});

    auto const initCode = infiniteLoopCreateInitCode();
    auto const gasLimit = createDepositGasWithExecutionBudget(initCode, 64);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeCreateDepositInput(stateView, vm, hash, sender, initCode, gasLimit);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.gasUsed, static_cast<int64_t>(gasLimit));
    BOOST_CHECK_EQUAL(nonceFromDiff(output.stateDiff, sender, initialNonce), initialNonce + 1);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, initialNonce);
}
}  // namespace bcos::evm::test
