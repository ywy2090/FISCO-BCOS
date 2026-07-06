/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-2929 CREATE predicted address warm semantics (geth: before Snapshot).
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

BOOST_AUTO_TEST_CASE(pre_snapshot_warm_survives_frame_revert)
{
    InMemoryStateView view;
    State state(view);
    auto const createAddr = addressFromByte(0x42);

    (void)state.warm_up_address_no_journal(createAddr);
    state.checkpoint();
    state.revert();

    BOOST_CHECK(state.is_address_warm(createAddr));
}

BOOST_AUTO_TEST_CASE(pre_snapshot_warm_survives_nested_revert)
{
    InMemoryStateView view;
    State state(view);
    auto const createAddr = addressFromByte(0x42);

    (void)state.warm_up_address_no_journal(createAddr);
    state.checkpoint();
    state.checkpoint();
    state.revert();
    state.revert();

    BOOST_CHECK(state.is_address_warm(createAddr));
}

BOOST_AUTO_TEST_CASE(pre_snapshot_warm_reverts_with_parent_after_successful_create)
{
    InMemoryStateView view;
    State state(view);
    auto const createAddr = addressFromByte(0x42);

    state.checkpoint();  // parent call frame
    (void)state.warm_up_address_no_journal(createAddr);
    state.checkpoint();  // CREATE frame
    state.journal_warm_address_for_revert(createAddr);
    state.commit();  // CREATE success → parent
    state.revert();  // parent REVERT

    BOOST_CHECK(!state.is_address_warm(createAddr));
}

BOOST_AUTO_TEST_CASE(pin_create_pre_snapshot_warm_survives_warm_journal_revert)
{
    InMemoryStateView view;
    State state(view);
    auto const createAddr = addressFromByte(0x42);

    state.checkpoint();
    state.pin_create_pre_snapshot_warm(createAddr);
    (void)state.warm_up_address(createAddr);
    state.revert();

    BOOST_CHECK(state.is_address_warm(createAddr));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
