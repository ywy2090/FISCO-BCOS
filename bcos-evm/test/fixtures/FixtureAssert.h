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
#include "bcos-evm/eth/gas/Eip7623.h"
#include "bcos-evm/eth/orchestration/normalizeIncludedTxVmerr.h"
#include "bcos-evm/eth/state/StateView.hpp"
#include <boost/test/unit_test.hpp>
#include <cstdlib>

namespace bcos::evm::test::fixtures
{

inline void assertFixtureResult(
    FixtureCase const& fixture, ExecuteViaEthOutput const& output, int64_t gasBefore)
{
    (void)gasBefore;
    evmc_status_code expectedStatus = fixture.expected.status;
    if (isTopLevelIncludedTxVmError(expectedStatus, output.executionContext.message.depth))
    {
        expectedStatus = EVMC_SUCCESS;
    }
    BOOST_CHECK_EQUAL(
        static_cast<int>(output.evmcResult.status_code), static_cast<int>(expectedStatus));
    bcos::bytes actual(output.evmcResult.output_data,
        output.evmcResult.output_data + output.evmcResult.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, fixture.expected.output),
        "output mismatch actual=0x" << bcos::toHex(actual) << " expected=0x"
                                    << bcos::toHex(fixture.expected.output));
    BOOST_CHECK_EQUAL(output.executionContext.logs.size(), fixture.expected.logs);
    auto const& message = output.executionContext.message;
    auto const& revision = output.executionContext.revisionConfig;
    int64_t const actualExecutorGas = message.gas - output.evmcResult.gas_left;
    int64_t reportedGas = actualExecutorGas;
    if (revision.eip7623)
    {
        auto const input = bcos::bytesConstRef(message.input_data, message.input_size);
        reportedGas += gas::calcEip7623Components(input).normalCost;
    }
    if (fixture.expected.gasUsedExecutor != 0)
    {
        int64_t const diff = std::abs(actualExecutorGas - fixture.expected.gasUsedExecutor);
        BOOST_CHECK_MESSAGE(diff <= fixture.expected.gasUsedExecutorTolerance,
            "executor gas mismatch actualGas=" << actualExecutorGas << " expectedGasUsedExecutor="
                                               << fixture.expected.gasUsedExecutor << " tolerance="
                                               << fixture.expected.gasUsedExecutorTolerance);
    }
    else if (fixture.expected.gasUsed != 0)
    {
        int64_t const diff = std::abs(reportedGas - fixture.expected.gasUsed);
        BOOST_CHECK_MESSAGE(diff <= fixture.expected.gasUsedTolerance,
            "gas mismatch actualGas=" << reportedGas
                                      << " expectedGasUsed=" << fixture.expected.gasUsed
                                      << " tolerance=" << fixture.expected.gasUsedTolerance);
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
