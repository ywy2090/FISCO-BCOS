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

#define BOOST_TEST_MODULE FrameValueTransferTest

#include "bcos-evm/eth/execution/FrameValueTransfer.h"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(nested_insufficient_balance_returns_false)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    bcos::evm_standard::RevisionConfig cfg{.revision = EVMC_CANCUN};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.sender =
        evmc_address{.bytes = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
    msg.recipient =
        evmc_address{.bytes = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}};
    msg.value.bytes[31] = 100;

    state.set_balance(msg.sender, 50);
    bool ok =
        execution::transferFrameValue(state, cfg, nullptr, msg, execution::FrameScope::Nested);
    BOOST_REQUIRE(!ok);
}
}  // namespace bcos::evm::test
