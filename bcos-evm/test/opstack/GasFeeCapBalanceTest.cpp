#define BOOST_TEST_MODULE GasFeeCapBalanceTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "helpers/InMemoryStateView.h"
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

BOOST_AUTO_TEST_CASE(gas_fee_cap_balance_check_rejects_insufficient_sender)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x81);
    auto const target = addressFromLastByte(0x82);
    stateView.insert_account(sender, state::Account{.balance = u256(10), .nonce = 0});
    stateView.insert_account(target, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    OpStackMessageRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.gasTipCap = 2;
    input.gasFeeCap = 10;
    input.blockInfo.baseFee = 1;
    input.revisionConfig.eip1559 = true;
    input.txProps.warmDestination = true;

    auto output = task::syncWait(applyOpStackMessage(input));
    BOOST_CHECK_EQUAL(output.evmcResult.status, protocol::TransactionStatus::NotEnoughCash);
}
}  // namespace bcos::evm::test
