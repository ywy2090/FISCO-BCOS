/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile router value-transfer envelope tests.
 *
 *  GAP-004 / Plan Task 1 gas preservation (geth parity):
 *  - ExecutionFrame.cpp transferOrFail + PrecompileRouter preserve message.gas on balance fail
 *  - GETH_ORACLE: go-ethereum/core/vm/evm.go:262-264 → return (nil, gas, ErrInsufficientBalance)
 */

#define BOOST_TEST_MODULE PrecompileRouterEnvelopeTest

#include "fixtures/EthFrameParityHelpers.h"
#include <boost/test/included/unit_test.hpp>
#include <array>

namespace bcos::evm::test
{
namespace
{
evmc_message valueTransferPrecompileMessage(evmc_address sender, evmc_address recipient,
    evmc_uint256be value, uint8_t const* inputData, size_t inputSize)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 500'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;
    message.value = value;
    message.input_data = inputData;
    message.input_size = inputSize;
    return message;
}
}  // namespace
BOOST_AUTO_TEST_CASE(c5_insufficient_balance_both_depths)
{
    constexpr int64_t kInputGas = 500'000;

    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryEvmStateReader view0;
    state::State state0(view0);
    state0.set_balance(sender, 99);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryEvmStateReader view1;
    state::State state1(view1);
    state1.set_balance(sender, 99);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, kInputGas);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, kInputGas);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
}

BOOST_AUTO_TEST_CASE(precompile_router_insufficient_balance_gas_preservation_characterization)
{
    constexpr int64_t kInputGas = 500'000;

    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryEvmStateReader view0;
    state::State state0(view0);
    state0.set_balance(sender, 99);
    auto const depth0 = runDepth0(state0, message);

    state::test::InMemoryEvmStateReader view1;
    state::State state1(view1);
    state1.set_balance(sender, 99);
    auto const depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, kInputGas);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, kInputGas);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, 99);
    BOOST_REQUIRE_EQUAL(depth1.senderBalance, 99);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, 0);
    BOOST_REQUIRE_EQUAL(depth1.recipientBalance, 0);
}

BOOST_AUTO_TEST_CASE(successful_value_transfer_balances_match_depth0_and_depth1)
{
    auto const sender = addressFromLastByte(0x01);
    auto const identity = precompileAddress(0x04);
    std::array<uint8_t, 4> inputBytes{0xde, 0xad, 0xbe, 0xef};
    auto const message = valueTransferMessage(sender, identity, weiValue(100), inputBytes);

    state::test::InMemoryEvmStateReader view0;
    state::State state0(view0);
    state0.set_balance(sender, 1'000'000);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryEvmStateReader view1;
    state::State state1(view1);
    state1.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, depth1.gasLeft);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
}

BOOST_AUTO_TEST_CASE(value_transfer_then_precompile_failure_reverts_balances_both_depths)
{
    auto const sender = addressFromLastByte(0x01);
    auto const bnPairing = precompileAddress(0x08);
    bcos::u256 const initialSenderBalance = 1'000'000;
    bcos::u256 const transferAmount = 100;

    // Pairing input must be a multiple of 192 bytes; 64 bytes → EVMC_PRECOMPILE_FAILURE.
    std::array<uint8_t, 64> invalidPairingInput{};
    auto const message = valueTransferPrecompileMessage(sender, bnPairing,
        weiValue(static_cast<uint8_t>(transferAmount)), invalidPairingInput.data(),
        invalidPairingInput.size());

    state::test::InMemoryEvmStateReader view0;
    state::State state0(view0);
    state0.set_balance(sender, initialSenderBalance);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryEvmStateReader view1;
    state::State state1(view1);
    state1.set_balance(sender, initialSenderBalance);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_PRECOMPILE_FAILURE);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, 0);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, 0);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, initialSenderBalance);
    BOOST_REQUIRE_EQUAL(depth1.senderBalance, initialSenderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, 0);
    BOOST_REQUIRE_EQUAL(depth1.recipientBalance, 0);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
}
}  // namespace bcos::evm::test
