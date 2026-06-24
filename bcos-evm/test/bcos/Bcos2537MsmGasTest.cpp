#define BOOST_TEST_MODULE Bcos2537MsmGasTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address precompileAddress(uint8_t lowByte)
{
    evmc_address addr{};
    addr.bytes[19] = lowByte;
    return addr;
}
}  // namespace

// executeViaHost delegates to executeMessage for kernel precompiles; assert MSM gas on that path.
BOOST_AUTO_TEST_CASE(g1msm_k2_gas_matches_geth_via_executeMessage_prague)
{
    state::test::InMemoryStateView view;
    auto const sender = precompileAddress(0x01);
    auto const g1MsmAddr = precompileAddress(0x0c);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    bcos::bytes input(320, 0);
    int64_t const initialGas = 500'000;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = initialGas;
    message.sender = sender;
    message.recipient = g1MsmAddr;
    message.code_address = g1MsmAddr;
    message.input_data = input.data();
    message.input_size = input.size();

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput execInput;
    execInput.stateView = &view;
    execInput.vm = &vm;
    execInput.message = message;
    execInput.blockInfo = blockInfo;
    execInput.revisionConfig.revision = EVMC_PRAGUE;
    execInput.revisionConfig.eip2537 = true;

    auto output = executeMessage(execInput);
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(initialGas - output.result.gas_left, 22776);
}

BOOST_AUTO_TEST_CASE(g1msm_k2_gas_matches_geth_via_executeMessage_isthmus_profile)
{
    state::test::InMemoryStateView view;
    auto const sender = precompileAddress(0x01);
    auto const g1MsmAddr = precompileAddress(0x0c);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    view.insert_account(sender, senderAccount);

    bcos::bytes input(320, 0);
    int64_t const initialGas = 500'000;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = initialGas;
    message.sender = sender;
    message.recipient = g1MsmAddr;
    message.code_address = g1MsmAddr;
    message.input_data = input.data();
    message.input_size = input.size();

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput execInput;
    execInput.stateView = &view;
    execInput.vm = &vm;
    execInput.message = message;
    execInput.blockInfo = blockInfo;
    execInput.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();

    auto output = executeMessage(execInput);
    BOOST_REQUIRE_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(initialGas - output.result.gas_left, 22776);
}

}  // namespace bcos::evm::test
