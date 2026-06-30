#define BOOST_TEST_MODULE Bcos7623PrecheckTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/FiscoExecute.h"
#include "bcos-evm/eth/gas/Eip7623.h"
#include "bcos-protocol/TransactionStatus.h"
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

BOOST_AUTO_TEST_CASE(fiscoExecute_web3Tx_eip7623_oog_when_gas_below_normal_cost)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    bcos::bytes calldata{0x01};
    auto const components = gas::calcEip7623Components(bcos::bytesConstRef(&calldata));
    BOOST_REQUIRE_GT(components.normalCost, 0);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = components.normalCost - 1;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    FiscoExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.web3Tx = true;
    input.revisionConfig.eth().revision = EVMC_PRAGUE;
    input.revisionConfig.eth().eip7623 = true;

    auto output = task::syncWait(applyFiscoMessage(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(static_cast<int>(output.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGas));
}

BOOST_AUTO_TEST_CASE(fiscoExecute_web3Tx_eip7623_skips_precheck_when_normal_cost_zero)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x03);
    auto const target = addressFromLastByte(0x04);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    bcos::bytes emptyCalldata;
    auto const components = gas::calcEip7623Components(bcos::bytesConstRef(&emptyCalldata));
    BOOST_CHECK_EQUAL(components.normalCost, 0);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.input_data = emptyCalldata.data();
    message.input_size = 0;

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    FiscoExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.web3Tx = true;
    input.revisionConfig.eth().revision = EVMC_PRAGUE;
    input.revisionConfig.eth().eip7623 = true;

    auto output = task::syncWait(applyFiscoMessage(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
}

}  // namespace bcos::evm::test
