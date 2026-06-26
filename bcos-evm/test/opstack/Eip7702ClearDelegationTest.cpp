#define BOOST_TEST_MODULE Eip7702ClearDelegationTest

#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/state/State.hpp"
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

BOOST_AUTO_TEST_CASE(auth_with_zero_target_clears_existing_delegation_code)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x61);
    auto const recipient = addressFromLastByte(0x62);
    auto const previousTarget = addressFromLastByte(0x63);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 100;
    senderAccount.code = addressToDelegation(previousTarget);
    stateView.insert_account(sender, std::move(senderAccount));
    stateView.insert_account(recipient, state::Account{});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 200'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    evmc::VM vm{evmc_create_evmone()};
    state::State state(stateView);
    ExecuteMessageInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.revisionConfig.revision = EVMC_CANCUN;
    input.revisionConfig.eip7702 = true;
    input.blockInfo.chainId = 1;
    input.authorizationListPresent = true;
    input.authorizations.push_back(
        {.chainId = u256(1), .authority = sender, .address = evmc_address{}, .nonce = 1});

    auto output = executeMessage(input);
    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);

    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    BOOST_CHECK(it->second.code.empty());
    BOOST_CHECK_EQUAL(it->second.nonce, uint64_t(2));
    BOOST_CHECK(state::Bytes32Equal{}(it->second.codeHash, state::emptyCodeHash()));
}
}  // namespace bcos::evm::test
