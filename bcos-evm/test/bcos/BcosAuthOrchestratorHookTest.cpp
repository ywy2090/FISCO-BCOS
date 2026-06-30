#define BOOST_TEST_MODULE BcosAuthOrchestratorHookTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/FiscoExecute.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos/adapters/InMemoryAuthAdapter.h"
#include "helpers/InMemoryEvmStateReader.h"
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

BOOST_AUTO_TEST_CASE(auth_checker_hook_short_circuits_before_executeMessage)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    stateView.insert_account(sender, state::Account{.balance = 1'000'000});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    FiscoExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.revisionConfig.enable_auth_check = true;
    InMemoryAuthAdapter authPort([](evmc_message const&) -> std::optional<EVMCResult> {
        evmc_result fail{};
        fail.status_code = EVMC_REJECTED;
        fail.gas_left = 0;
        return EVMCResult(fail, protocol::TransactionStatus::PermissionDenied);
    });
    input.authPort = &authPort;

    auto output = task::syncWait(fiscoExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REJECTED);
    BOOST_CHECK(output.stateDiff.accounts.find(target) == output.stateDiff.accounts.end());
}

}  // namespace bcos::evm::test
