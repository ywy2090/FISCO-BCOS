/*
 *  Copyright (C) 2026 FISCO BCOS.
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

#define BOOST_TEST_MODULE FrameTargetResolverTest

#include "bcos-evm/eth/kernel/execution/FrameTargetResolver.h"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
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
    return {.revision = EVMC_PRAGUE, .eip2929 = true, .eip7702 = true};
}

void requireAddressEqual(evmc_address const& actual, evmc_address const& expected)
{
    BOOST_REQUIRE(std::memcmp(actual.bytes, expected.bytes, sizeof(actual.bytes)) == 0);
}
}  // namespace

BOOST_AUTO_TEST_CASE(nested_create_fills_recipient_and_pins_warm)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.recipient = addr(0x42);
    msg.code_address = {};

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::Nested);
    requireAddressEqual(resolved.routed.code_address, addr(0x42));
}

BOOST_AUTO_TEST_CASE(nested_call_normalizes_code_address_for_identity)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = identity;
    msg.code_address = identity;

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::Nested);
    requireAddressEqual(resolved.executionAddress, identity);
    requireAddressEqual(resolved.routed.code_address, identity);
}

BOOST_AUTO_TEST_CASE(top_level_skips_create_warm_pin)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.recipient = addr(0x55);
    msg.code_address = {};

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::TopLevel);
    requireAddressEqual(resolved.routed.recipient, addr(0x55));
    BOOST_REQUIRE(std::memcmp(resolved.routed.code_address.bytes, evmc_address{}.bytes, 20) == 0);
}

BOOST_AUTO_TEST_CASE(top_level_call_zero_code_address_fills_recipient)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto recipient = addr(0x77);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = recipient;
    msg.code_address = {};

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::TopLevel);
    requireAddressEqual(resolved.executionAddress, recipient);
    requireAddressEqual(resolved.routed.code_address, recipient);
}

BOOST_AUTO_TEST_CASE(nested_7702_call_pins_authority_as_execution_address)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto const authority = addr(0xAA);
    auto const identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = EVMC_DELEGATED;
    msg.recipient = authority;
    msg.code_address = identity;

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::Nested);
    requireAddressEqual(resolved.executionAddress, authority);
    requireAddressEqual(resolved.routed.code_address, authority);
}

BOOST_AUTO_TEST_CASE(nested_7702_delegatecall_keeps_delegate_in_execution_address)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto const caller = addr(0x02);
    auto const identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.flags = EVMC_DELEGATED;
    msg.recipient = caller;
    msg.code_address = identity;

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::Nested);
    requireAddressEqual(resolved.executionAddress, identity);
}

BOOST_AUTO_TEST_CASE(top_level_7702_call_uses_authority_as_execution_address)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto const authority = addr(0xAB);
    auto const identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = EVMC_DELEGATED;
    msg.recipient = authority;
    msg.code_address = identity;

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::TopLevel);
    requireAddressEqual(resolved.executionAddress, authority);
}

BOOST_AUTO_TEST_CASE(nested_7702_staticcall_delegated_uses_code_address)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    auto cfg = pragueCfg();
    auto const authority = addr(0xAC);
    auto const identity = addr(0x04);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = EVMC_STATIC | EVMC_DELEGATED;
    msg.recipient = authority;
    msg.code_address = identity;

    auto resolved = execution::resolveFrameTarget(state, cfg, msg, execution::FrameScope::Nested);
    requireAddressEqual(resolved.executionAddress, identity);
}
}  // namespace bcos::evm::test
