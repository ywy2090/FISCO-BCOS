#define BOOST_TEST_MODULE Eip7702PreCheckTest

#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "helpers/InMemoryStateView.h"
#include "helpers/OpStackEntryStateTransitionHooks.h"
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

OpStackMessageRequest makeInput(evmc_address sender)
{
    OpStackMessageRequest input;
    input.message.kind = EVMC_CALL;
    input.message.sender = sender;
    input.message.gas = 50'000;
    input.nonce = 0;
    input.blockInfo.baseFee = 1;
    input.gasTipCap = 1;
    input.gasFeeCap = 1;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(rejects_authorization_list_on_create)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x10);
    state::State state(stateView);

    auto input = makeInput(sender);
    input.message.kind = EVMC_CREATE;
    input.authorizations.push_back({});

    auto error = runOpStackEntryLifecycleCheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(rejects_explicit_empty_authorization_list)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x11);
    state::State state(stateView);

    auto input = makeInput(sender);
    input.authorizationListPresent = true;
    input.authorizations.clear();

    auto error = runOpStackEntryLifecycleCheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}
}  // namespace bcos::evm::test
