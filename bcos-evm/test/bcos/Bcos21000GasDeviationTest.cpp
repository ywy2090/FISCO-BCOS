#define BOOST_TEST_MODULE Bcos21000GasDeviationTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

using namespace bcos;
using namespace bcos::evm;

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

BOOST_AUTO_TEST_SUITE(Bcos21000GasDeviationTest)

BOOST_AUTO_TEST_CASE(balance_transfer_gas_constant_documents_deviation)
{
    BOOST_CHECK_EQUAL(BALANCE_TRANSFER_GAS, 21'000);
}

BOOST_AUTO_TEST_CASE(executeViaHost_debits_balance_transfer_gas_before_evm)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    int64_t const initialGas = 50'000;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = initialGas;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    ExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.eth().revision = EVMC_CANCUN;

    auto output = task::syncWait(executeViaHost(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.executionContext.message.gas, initialGas - BALANCE_TRANSFER_GAS);
}

BOOST_AUTO_TEST_SUITE_END()
