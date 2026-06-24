#define BOOST_TEST_MODULE CanTransferTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/OpStackConstants.h"
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

OpStackExecutionRequest makeInput(state::test::InMemoryEvmStateReader& stateView, evmc::VM& vm,
    crypto::Hash const& hash, evmc_address sender, evmc_address recipient)
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
    input.blockInfo.baseFee = 1;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    input.revisionConfig.eip1559 = true;
    input.revisionConfig.eip7623 = true;
    input.revisionConfig.eip7702 = true;
    input.revisionConfig.eip4844 = true;
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{};
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(value_transfer_rejected_when_sender_balance_insufficient)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x61);
    auto const target = addressFromLastByte(0x62);
    stateView.insert_account(sender, state::Account{.balance = u256(5), .nonce = 0});
    stateView.insert_account(target, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeInput(stateView, vm, hash, sender, target);
    input.message.value.bytes[31] = 10;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{.from = sender,
        .to = target,
        .mint = u256(0),
        .value = u256(10),
        .gas = static_cast<uint64_t>(input.message.gas)};
    auto output = task::syncWait(opStackExecute(input));

    BOOST_CHECK_EQUAL(output.evmcResult.status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(transfer_to_predeploy_allowed_if_funded)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x63);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeInput(stateView, vm, hash, sender, OP_L1_BLOCK_PREDEPLOY);
    input.message.value.bytes[31] = 1;
    auto output = task::syncWait(opStackExecute(input));

    BOOST_CHECK(output.evmcResult.status != protocol::TransactionStatus::NotEnoughCash);
    BOOST_CHECK(output.evmcResult.status != protocol::TransactionStatus::InsufficientFunds);
}
}  // namespace bcos::evm::test
