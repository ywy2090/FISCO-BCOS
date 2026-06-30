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

#define BOOST_TEST_MODULE StateRefundTest
#include "bcos-evm/eth/state/State.hpp"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::state::test
{
namespace
{
class EmptyStateView : public StateView
{
public:
    std::optional<Account> get_account(const evmc_address&) const override { return std::nullopt; }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(StateRefundTest)

BOOST_AUTO_TEST_CASE(Refund_startsAtZero)
{
    EmptyStateView view;
    State state(view);
    BOOST_CHECK_EQUAL(state.get_refund(), 0u);
}

BOOST_AUTO_TEST_CASE(Refund_accumulates)
{
    EmptyStateView view;
    State state(view);
    state.add_refund(4800);
    state.add_refund(15000);
    BOOST_CHECK_EQUAL(state.get_refund(), 19800u);
}

BOOST_AUTO_TEST_CASE(Refund_clearRefund)
{
    EmptyStateView view;
    State state(view);
    state.add_refund(4800);
    state.clear_refund();
    BOOST_CHECK_EQUAL(state.get_refund(), 0u);
}

BOOST_AUTO_TEST_CASE(Refund_clearedOnRevert)
{
    EmptyStateView view;
    State state(view);
    state.checkpoint();
    state.add_refund(4800);
    state.revert();
    BOOST_CHECK_EQUAL(state.get_refund(), 0u);
}

BOOST_AUTO_TEST_CASE(Refund_preservedOnCommit)
{
    EmptyStateView view;
    State state(view);
    state.checkpoint();
    state.add_refund(4800);
    state.commit();
    BOOST_CHECK_EQUAL(state.get_refund(), 4800u);
}

BOOST_AUTO_TEST_CASE(Refund_revertRestoresPreCheckpointValue)
{
    EmptyStateView view;
    State state(view);
    state.add_refund(1000);
    state.checkpoint();
    state.add_refund(4800);
    state.revert();
    BOOST_CHECK_EQUAL(state.get_refund(), 1000u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::state::test
