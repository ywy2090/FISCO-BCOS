#define BOOST_TEST_MODULE Eip7823ModexpRejectTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "state/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address modexpAddress()
{
    evmc_address addr{};
    addr.bytes[19] = 0x05;
    return addr;
}

evmc_address blake2fAddress()
{
    evmc_address addr{};
    addr.bytes[19] = 0x09;
    return addr;
}

evmc_address senderAddress()
{
    evmc_address addr{};
    addr.bytes[19] = 0x01;
    return addr;
}

bcos::bytes modexpHeaderBaseLen1025()
{
    bcos::bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(osaka_modexp_field_1025_rejected_via_executeMessage)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = senderAddress();
    auto const modexp = modexpAddress();

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    auto const input = modexpHeaderBaseLen1025();
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = modexp;
    message.code_address = modexp;
    message.input_data = input.data();
    message.input_size = input.size();

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput execInput;
    execInput.stateView = &stateView;
    execInput.vm = &vm;
    execInput.message = message;
    execInput.blockInfo = blockInfo;
    execInput.revisionConfig.revision = EVMC_OSAKA;
    execInput.revisionConfig.eip7823 = true;

    auto output = executeMessage(std::move(execInput));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_PRECOMPILE_FAILURE);
}

BOOST_AUTO_TEST_CASE(osaka_modexp_field_1025_rejected_via_dispatch)
{
    auto const modexp = modexpAddress();
    auto const input = modexpHeaderBaseLen1025();
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_OSAKA, .eip7823 = true};

    auto result = state::EthPrecompiles::dispatch(
        modexp, bcos::bytesConstRef(input.data(), input.size()), 500'000, EVMC_OSAKA, cfg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status, EVMC_PRECOMPILE_FAILURE);
}

BOOST_AUTO_TEST_CASE(prague_modexp_field_1025_not_rejected)
{
    auto const modexp = modexpAddress();
    auto const input = modexpHeaderBaseLen1025();
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip7823 = false};

    auto result = state::EthPrecompiles::dispatch(
        modexp, bcos::bytesConstRef(input.data(), input.size()), 500'000, EVMC_PRAGUE, cfg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(precompile_failure_exhausts_call_gas)
{
    auto const blake2f = blake2fAddress();
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_PRAGUE};

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    message.code_address = blake2f;
    message.input_data = nullptr;
    message.input_size = 0;

    auto result = state::EthPrecompiles::tryDispatchInCall(blake2f, message, EVMC_PRAGUE, cfg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status_code, EVMC_PRECOMPILE_FAILURE);
    BOOST_CHECK_EQUAL(result->gas_left, 0);
}

}  // namespace bcos::evm::test
