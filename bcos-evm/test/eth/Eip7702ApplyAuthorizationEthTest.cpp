#define BOOST_TEST_MODULE Eip7702ApplyAuthorizationEthTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(apply_authorization_via_executeMessage_prague)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x31);
    auto const recipient = addressFromLastByte(0x32);
    auto const delegationTarget = addressFromLastByte(0x42);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 200'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    state::BlockInfo blockInfo;
    blockInfo.number = 1;
    blockInfo.chainId = 1;
    blockInfo.gasLimit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.eip7702 = true;
    input.authorizationListPresent = true;
    input.authorizations.push_back(
        {.chainId = u256(1), .authority = sender, .address = delegationTarget, .nonce = 1});

    auto output = executeMessage(std::move(input));
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    auto const& installedCode = it->second.code;
    BOOST_CHECK_EQUAL(installedCode.size(), size_t(23));
    BOOST_CHECK_EQUAL(installedCode[0], 0xEF);
    BOOST_CHECK_EQUAL(installedCode[1], 0x01);
    BOOST_CHECK_EQUAL(installedCode[2], 0x00);
    BOOST_CHECK_EQUAL(it->second.nonce, uint64_t(2));
    auto const expectedHash =
        state::keccak256Code(bcos::bytesConstRef{installedCode.data(), installedCode.size()});
    BOOST_CHECK(state::Bytes32Equal{}(it->second.codeHash, expectedHash));
}

}  // namespace bcos::evm::test
