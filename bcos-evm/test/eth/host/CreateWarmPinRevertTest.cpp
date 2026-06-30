/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief CREATE warm pin is reverted when deployment fails; survives nested REVERT.
 */

#define BOOST_TEST_MODULE CreateWarmPinRevertTest

#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::state::test
{
namespace
{
evmc_address addressFromByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CreateWarmPinRevertTest)

BOOST_AUTO_TEST_CASE(failed_create_reverts_warm_pin)
{
    InMemoryStateView view;
    State state(view);
    auto const createAddr = addressFromByte(0x42);

    state.checkpoint();
    state.pin_warm_create_address(createAddr);
    BOOST_CHECK(state.is_address_warm(createAddr));

    state.revert();
    BOOST_CHECK(!state.is_address_warm(createAddr));
}

BOOST_AUTO_TEST_CASE(create_pin_survives_nested_revert)
{
    InMemoryStateView view;
    State state(view);
    auto const createAddr = addressFromByte(0x42);

    state.checkpoint();
    state.pin_warm_create_address(createAddr);
    BOOST_CHECK(state.is_address_warm(createAddr));

    state.checkpoint();
    state.revert();

    BOOST_CHECK(state.is_address_warm(createAddr));

    state.revert();
    BOOST_CHECK(!state.is_address_warm(createAddr));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
