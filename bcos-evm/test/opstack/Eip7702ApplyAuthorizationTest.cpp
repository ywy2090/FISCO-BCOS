#define BOOST_TEST_MODULE Eip7702ApplyAuthorizationTest

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "state/InMemoryStateView.h"
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

BOOST_AUTO_TEST_CASE(valid_auth_installs_delegation_invalid_is_ignored_and_refund_added)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x31);
    auto const recipient = addressFromLastByte(0x32);
    auto const delegationTarget = addressFromLastByte(0x42);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 100;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 200'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    evmc::VM vm{evmc_create_evmone()};
    ExecuteMessageInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.message = message;
    input.revisionConfig.revision = EVMC_CANCUN;
    input.revisionConfig.eip7702 = true;
    input.blockInfo.chainId = 1;
    input.authorizationListPresent = true;
    input.authorizations.push_back(
        {.chainId = u256(1), .authority = sender, .address = delegationTarget, .nonce = 1});
    input.authorizations.push_back(
        {.chainId = u256(1), .authority = sender, .address = delegationTarget, .nonce = 99});

    auto output = executeMessage(input);
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    auto const& installedCode = it->second.code;
    BOOST_CHECK_EQUAL(installedCode.size(), size_t(23));
    BOOST_CHECK_EQUAL(installedCode[0], 0xEF);
    BOOST_CHECK_EQUAL(installedCode[1], 0x01);
    BOOST_CHECK_EQUAL(installedCode[2], 0x00);
    BOOST_CHECK_EQUAL(it->second.nonce, uint64_t(2));
}

}  // namespace bcos::evm::test
