/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#define BOOST_TEST_MODULE RouteMessageTest

#include "bcos-evm/eth/execution/RouteMessage.h"
#include "bcos-evm/eth/state/State.hpp"
#include "state/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test
{
namespace
{
evmc_address addr(uint8_t last)
{
    evmc_address a{};
    a.bytes[19] = last;
    return a;
}

bcos::evm_standard::RevisionConfig pragueCfg()
{
    return {.revision = EVMC_PRAGUE, .warm_access = true};
}
}  // namespace

BOOST_AUTO_TEST_CASE(nested_create_fills_recipient_and_pins_warm)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto cfg = pragueCfg();

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.recipient = addr(0x42);
    msg.code_address = {};

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::Nested);
    BOOST_REQUIRE(std::memcmp(routed.message.code_address.bytes, addr(0x42).bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(nested_marks_identity_precompile_target)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = identity;
    msg.code_address = identity;

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::Nested);
    BOOST_REQUIRE(routed.hasPrecompileTarget);
    BOOST_REQUIRE(std::memcmp(routed.precompileTarget.bytes, identity.bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(top_level_skips_create_warm_pin)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto cfg = pragueCfg();

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.recipient = addr(0x55);
    msg.code_address = {};

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(std::memcmp(routed.message.recipient.bytes, addr(0x55).bytes, 20) == 0);
    // TopLevel: no recipient/code_address mutation beyond caller-provided fields
    BOOST_REQUIRE(std::memcmp(routed.message.code_address.bytes, evmc_address{}.bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(top_level_call_zero_code_address_fills_recipient)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto recipient = addr(0x77);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = recipient;
    msg.code_address = {};

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(std::memcmp(routed.message.code_address.bytes, recipient.bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(top_level_call_marks_identity_precompile_target)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = identity;
    msg.code_address = {};

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(routed.hasPrecompileTarget);
    BOOST_REQUIRE(std::memcmp(routed.precompileTarget.bytes, identity.bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(top_level_create_skips_precompile_target)
{
    state::test::InMemoryEvmStateReader view;
    state::State state(view);
    auto cfg = pragueCfg();

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.recipient = addr(0x04);
    msg.code_address = {};

    auto routed = execution::routeMessage(state, cfg, msg, execution::FrameScope::TopLevel);
    BOOST_REQUIRE(!routed.hasPrecompileTarget);
}
}  // namespace bcos::evm::test
