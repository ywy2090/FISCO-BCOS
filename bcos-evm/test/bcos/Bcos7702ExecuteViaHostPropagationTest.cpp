#define BOOST_TEST_MODULE Bcos7702ExecuteViaHostPropagationTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
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

BOOST_AUTO_TEST_CASE(executeViaHost_propagates_authorizations_to_executeMessage)
{
    state::test::InMemoryStateView stateView;
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
    FakeHash hash;
    ExecuteViaHostInput input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig.eth().revision = EVMC_PRAGUE;
    input.revisionConfig.eth().eip7702 = true;
    input.authorizationListPresent = true;
    input.authorizations.push_back(
        {.chainId = u256(1), .authority = sender, .address = delegationTarget, .nonce = 0});

    auto output = task::syncWait(executeViaHost(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);

    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    auto const& installedCode = it->second.code;
    BOOST_CHECK_EQUAL(installedCode.size(), size_t(23));
    BOOST_CHECK_EQUAL(installedCode[0], 0xEF);
    BOOST_CHECK_EQUAL(installedCode[1], 0x01);
    BOOST_CHECK_EQUAL(installedCode[2], 0x00);
    BOOST_CHECK_EQUAL(it->second.nonce, uint64_t(1));
}

}  // namespace bcos::evm::test
