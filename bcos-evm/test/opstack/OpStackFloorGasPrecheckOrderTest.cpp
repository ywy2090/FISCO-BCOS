#define BOOST_TEST_MODULE OpStackPreDebitOrderTest

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include "bcos-evm/opstack/fee/OpStackFloorGasPrecheck.h"
#include "helpers/InMemoryStateView.h"
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

bytesConstRef toRef(bytes const& data)
{
    return {data.data(), data.size()};
}
}  // namespace

BOOST_AUTO_TEST_CASE(insufficient_balance_returns_insufficient_balance)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x61);
    stateView.insert_account(sender, state::Account{.balance = u256(5), .nonce = 0});
    state::State state(stateView);

    evmc_message message{};
    message.sender = sender;
    message.value.bytes[31] = 10;

    uint64_t floorOut = 0;
    auto const result = opStackFloorGasPrecheck({.message = message,
        .state = state,
        .gasLimit = 100'000,
        .skipTransactionChecks = false,
        .inputData = {},
        .floorDataGasOut = floorOut});

    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status_code, EVMC_INSUFFICIENT_BALANCE);
}

BOOST_AUTO_TEST_CASE(floor_failure_returns_out_of_gas)
{
    bytes data(100, 0xff);
    auto const floor = floorDataGas(toRef(data));

    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x62);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);

    evmc_message message{};
    message.sender = sender;
    message.input_data = data.data();
    message.input_size = data.size();
    message.gas = static_cast<int64_t>(floor);

    uint64_t floorOut = 0;
    auto const result = opStackFloorGasPrecheck({.message = message,
        .state = state,
        .gasLimit = floor - 1,
        .skipTransactionChecks = true,
        .inputData = toRef(data),
        .floorDataGasOut = floorOut});

    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(floorOut, floor);
}

BOOST_AUTO_TEST_CASE(success_writes_floor_without_subtracting_gas)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x63);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);

    evmc_message message{};
    message.sender = sender;
    message.gas = 50'000;

    uint64_t floorOut = 0;
    auto const gasBefore = message.gas;
    auto const result = opStackFloorGasPrecheck({.message = message,
        .state = state,
        .gasLimit = 50'000,
        .skipTransactionChecks = true,
        .inputData = {},
        .floorDataGasOut = floorOut});

    BOOST_CHECK(!result.has_value());
    BOOST_CHECK_EQUAL(floorOut, floorDataGas({}));
    BOOST_CHECK_EQUAL(message.gas, gasBefore);
}
}  // namespace bcos::evm::test
