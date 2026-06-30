#define BOOST_TEST_MODULE Eip7702ApplyAuthorizationEthTest

#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include "helpers/SetCodeAuthorizationTestHelper.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(apply_authorization_via_innerExecute_prague)
{
    auto const authKey = TestAuthKeyPair::generate();
    auto const sender = authKey.address();
    evmc_address recipient{};
    recipient.bytes[19] = 0x32;
    evmc_address delegationTarget{};
    delegationTarget.bytes[19] = 0x42;

    state::test::InMemoryStateView stateView;
    stateView.insert_account(sender, state::Account{.balance = 1'000'000, .nonce = 0});
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
    state::State state(stateView);
    InnerExecuteInput input;
    input.state = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.eip7702 = true;
    input.authorizationListPresent = true;
    input.authorizations.push_back(authKey.sign(delegationTarget, 1));

    auto output = innerExecute(std::move(input));
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
