#define BOOST_TEST_MODULE BlobGasBalanceTest

#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/opstack/OpStackPreCheck.h"
#include "state/InMemoryStateView.h"
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

BOOST_AUTO_TEST_CASE(blob_gas_fee_cap_under_blob_base_fee_is_rejected)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x91);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);

    OpStackExecuteViaHostInput input;
    input.message.kind = EVMC_CALL;
    input.message.gas = 100'000;
    input.message.sender = sender;
    input.message.recipient = addressFromLastByte(0x92);
    input.message.code_address = input.message.recipient;
    input.nonce = 0;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.revisionConfig.eip4844 = true;
    input.blockInfo.baseFee = 1;
    input.blockInfo.blobBaseFee = 100;
    input.blobVersionedHashes.push_back(h256(1));
    input.blobGasFeeCap = 99;

    auto error = opStackPreCheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}
}  // namespace bcos::evm::test
