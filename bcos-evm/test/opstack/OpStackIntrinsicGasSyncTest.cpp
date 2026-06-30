#define BOOST_TEST_MODULE OpStackIntrinsicGasSyncTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/opstack/OpStackChainPolicy.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-evm/opstack/OpStackExecuteMessageTestHook.h"
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
}  // namespace

BOOST_AUTO_TEST_CASE(execute_message_receives_debited_intrinsic_gas)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const recipient = addressFromLastByte(0x02);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    OpStackExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{.from = sender, .to = recipient, .gas = 100'000};
    input.skipTransactionChecks = true;
    input.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();
    input.blockInfo.number = 1;
    input.blockInfo.timestamp = 1;
    input.blockInfo.gasLimit = 30'000'000;

    int64_t capturedGas = -1;
    opstack::test::setExecuteMessageSpy([&](ExecuteMessageInput const& execInput) {
        capturedGas = execInput.message.gas;
        evmc_result raw{};
        raw.status_code = EVMC_SUCCESS;
        raw.gas_left = execInput.message.gas;
        return ExecuteMessageOutput{.result = evmc::Result{raw}};
    });

    auto output = task::syncWait(opStackExecute(input));
    opstack::test::clearExecuteMessageSpy();

    auto const intrinsic =
        gas::computeTxIntrinsicGas(message, input.accessList, input.web3TypedTxKind);
    auto const intrinsicDebit = static_cast<int64_t>(intrinsic.preExecutionDebit()) +
                                gas::calcAuthTupleIntrinsicGas(input.authorizations.size());

    BOOST_REQUIRE(capturedGas >= 0);
    BOOST_CHECK_EQUAL(capturedGas, message.gas - intrinsicDebit);
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
}
}  // namespace bcos::evm::test
