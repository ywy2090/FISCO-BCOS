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
 *
 * @brief Shared assertions for EthTransactionExecutor fixture validation.
 * @file ExecutorFixtureAssert.h
 */

#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/test/unit_test.hpp>
#include <cstdlib>

namespace bcos::evm::test::fixtures
{

enum class AssertPhase
{
    Phase1,
    Phase2,
};

inline int32_t expectedReceiptStatus(evmc_status_code evmcStatus)
{
    using bcos::protocol::TransactionStatus;
    switch (evmcStatus)
    {
    case EVMC_SUCCESS:
        return static_cast<int32_t>(TransactionStatus::None);
    case EVMC_REVERT:
        return static_cast<int32_t>(TransactionStatus::RevertInstruction);
    case EVMC_OUT_OF_GAS:
        return static_cast<int32_t>(TransactionStatus::OutOfGasLimit);
    default:
        return static_cast<int32_t>(TransactionStatus::Unknown);
    }
}

inline void assertExecutorFixtureResult(FixtureCase const& fixture,
    bcos::protocol::TransactionReceipt const& receipt, AssertPhase phase = AssertPhase::Phase1)
{
    BOOST_CHECK_EQUAL(receipt.status(), expectedReceiptStatus(fixture.expected.status));
    bcos::bytes actualOutput(receipt.output().begin(), receipt.output().end());
    BOOST_CHECK_MESSAGE(sameBytes(actualOutput, fixture.expected.output),
        "output mismatch actual=0x" << bcos::toHex(receipt.output()) << " expected=0x"
                                    << bcos::toHex(fixture.expected.output));
    BOOST_CHECK_EQUAL(receipt.logEntries().size(), fixture.expected.logs);
    if (phase == AssertPhase::Phase2 && fixture.expected.gasUsedExecutor != 0)
    {
        int64_t const actual = static_cast<int64_t>(receipt.gasUsed());
        int64_t const diff = std::abs(actual - fixture.expected.gasUsedExecutor);
        BOOST_CHECK_LE(diff, fixture.expected.gasUsedExecutorTolerance);
    }
}

}  // namespace bcos::evm::test::fixtures
