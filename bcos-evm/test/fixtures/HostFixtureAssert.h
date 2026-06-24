#pragma once

#include "EthStateFixtureLoader.h"
#include "FiscoFixtureAdapter.h"
#include "bcos-evm/bcos/FiscoExecutionBridge.h"
#include "bcos-evm/eth/gas/Eip7623.h"
#include <boost/test/unit_test.hpp>

namespace bcos::evm::test::fixtures
{

inline void assertHostFixtureResult(
    FixtureCase const& fixture, FiscoExecutionResult const& output, int64_t gasBefore)
{
    (void)gasBefore;
    BOOST_CHECK_EQUAL(
        static_cast<int>(output.evmcResult.status_code), static_cast<int>(fixture.expected.status));
    bcos::bytes actual(output.evmcResult.output_data,
        output.evmcResult.output_data + output.evmcResult.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, fixture.expected.output),
        "output mismatch actual=0x" << bcos::toHex(actual) << " expected=0x"
                                    << bcos::toHex(fixture.expected.output));
    auto const& message = output.executionContext.message;
    auto const revision = revisionConfigFromFixtureRevision(fixture.revision);
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

}  // namespace bcos::evm::test::fixtures
