/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Precompile router value-transfer envelope tests.
 */

#define BOOST_TEST_MODULE PrecompileRouterEnvelopeTest

#include "fixtures/EthFrameParityHelpers.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(c5_insufficient_balance_both_depths)
{
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
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, 0);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, 0);
    BOOST_REQUIRE_EQUAL(depth0.senderBalance, depth1.senderBalance);
    BOOST_REQUIRE_EQUAL(depth0.recipientBalance, depth1.recipientBalance);
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
}  // namespace bcos::evm::test
