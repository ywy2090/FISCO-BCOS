#define BOOST_TEST_MODULE Eip7702DelegationSenderTest

#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "helpers/InMemoryStateView.h"
#include "helpers/OpStackEntryStateTransitionHooks.h"
#include <boost/test/included/unit_test.hpp>
#include "bcos-evm/eth/state/State.hpp"

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

OpStackMessageRequest makeInput(evmc_address sender)
{
    OpStackMessageRequest input;
    input.message.kind = EVMC_CALL;
    input.message.sender = sender;
    input.message.gas = 80'000;
    input.nonce = 0;
    input.blockInfo.baseFee = 1;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(sender_with_delegation_code_passes_precheck)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x41);
    auto const delegationTarget = addressFromLastByte(0x42);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.code = addressToDelegation(delegationTarget);
    stateView.insert_account(sender, std::move(senderAccount));

    state::State state(stateView);
    auto input = makeInput(sender);

    auto error = runOpStackEntryLifecycleCheck(input, stateView);
    BOOST_CHECK(!error.has_value());
}

BOOST_AUTO_TEST_CASE(sender_with_non_delegation_code_is_rejected)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x51);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.code = {0x60, 0x00, 0x60, 0x00, 0xf3};
    stateView.insert_account(sender, std::move(senderAccount));

    state::State state(stateView);
    auto input = makeInput(sender);

    auto error = runOpStackEntryLifecycleCheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}
}  // namespace bcos::evm::test
