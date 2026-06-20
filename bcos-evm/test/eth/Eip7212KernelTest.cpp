#define BOOST_TEST_MODULE Eip7212KernelTest

#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address p256Address()
{
    evmc_address addr{};
    addr.bytes[18] = 0x01;
    addr.bytes[19] = 0x00;
    return addr;
}

evmc_address senderAddress()
{
    evmc_address addr{};
    addr.bytes[19] = 0x01;
    return addr;
}

bcos::bytes p256verifyValidSignatureInput()
{
    bcos::bytes input;
    input += bcos::fromHex("bb5a52f42f9c9261ed4361f59422a1e30036e7c32b270c8807a419feca605023");
    input += bcos::fromHex("2ba3a8be6b94d5ec80a6d9d1190a436effe50d85a1eee859b8cc6af9bd5c2e18");
    input += bcos::fromHex("4cd60b855d442f5b3c7b11eb6c4e0ae7525fe710fab9aa7c77a67f79e6fadd76");
    input += bcos::fromHex("2927b10512bae3eddcfe467828128bad2903269919f7086069c8c4df6c732838");
    input += bcos::fromHex("c7787964eaac00e5921fb1498a60f4606766b3d9685001558d1a974e7341513e");
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(p256verify_osaka_success_via_dispatch)
{
    auto const addr = p256Address();
    auto const input = p256verifyValidSignatureInput();
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_OSAKA, .eip7212 = true};

    auto result = state::EthPrecompiles::dispatch(
        addr, bcos::bytesConstRef(input.data(), input.size()), 500'000, EVMC_OSAKA, cfg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result->gasCost, 6900);
    BOOST_REQUIRE_EQUAL(result->output.size(), 32u);
    BOOST_CHECK_EQUAL(result->output[31], 0x01);
}

BOOST_AUTO_TEST_CASE(p256verify_osaka_success_via_executeMessage)
{
    state::test::InMemoryStateView stateView;
    auto const sender = senderAddress();
    auto const p256 = p256Address();

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    auto const input = p256verifyValidSignatureInput();
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = p256;
    message.code_address = p256;
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
    execInput.revisionConfig.eip7212 = true;

    auto output = executeMessage(std::move(execInput));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(output.result.output_size, 32u);
    BOOST_CHECK_EQUAL(output.result.output_data[31], 0x01);
}

}  // namespace bcos::evm::test
