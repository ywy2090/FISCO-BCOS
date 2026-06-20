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
 *
 * @brief Shared assertions for executeViaEth fixture validation.
 * @file FixtureAssert.h
 */

#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-evm/eth/state/StateView.hpp"
#include <boost/test/unit_test.hpp>
#include <cstdlib>

namespace bcos::evm::test::fixtures
{

inline void assertFixtureResult(
    FixtureCase const& fixture, ExecuteViaEthOutput const& output, int64_t gasBefore)
{
    BOOST_CHECK_EQUAL(
        static_cast<int>(output.evmcResult.status_code), static_cast<int>(fixture.expected.status));
    bcos::bytes actual(output.evmcResult.output_data,
        output.evmcResult.output_data + output.evmcResult.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, fixture.expected.output),
        "output mismatch actual=0x" << bcos::toHex(actual) << " expected=0x"
                                    << bcos::toHex(fixture.expected.output));
    BOOST_CHECK_EQUAL(output.executionContext.logs.size(), fixture.expected.logs);
    if (fixture.expected.gasUsed != 0)
    {
        int64_t const actualGas = gasBefore - output.evmcResult.gas_left;
        int64_t const diff = std::abs(actualGas - fixture.expected.gasUsed);
        BOOST_CHECK_LE(diff, fixture.expected.gasUsedTolerance);
    }
}

inline void assertFixturePostState(state::StateView const& stateView, FixtureCase const& fixture)
{
    for (auto const& expectedPost : fixture.expected.post)
    {
        BOOST_TEST_CONTEXT("post address=0x" << bcos::toHex(bcos::bytesConstRef(
                               expectedPost.address.bytes, sizeof(expectedPost.address.bytes))))
        {
            if (expectedPost.balance.has_value())
            {
                BOOST_CHECK_EQUAL(
                    stateView.get_balance(expectedPost.address), *expectedPost.balance);
            }
            auto const code = stateView.get_code(expectedPost.address);
            if (expectedPost.codeEmpty.value_or(false))
            {
                BOOST_CHECK_MESSAGE(code.empty(), "expected empty code after selfdestruct");
            }
            if (expectedPost.codeNonempty.value_or(false))
            {
                BOOST_CHECK_MESSAGE(!code.empty(), "expected code retained under EIP-6780");
            }
        }
    }
}

}  // namespace bcos::evm::test::fixtures
