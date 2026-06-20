#pragma once

#include "EthStateFixtureLoader.h"
#include "bcos-evm/bcos/ExecuteViaHost.h"
#include <boost/test/unit_test.hpp>

namespace bcos::evm::test::fixtures
{

inline void assertHostFixtureResult(
    FixtureCase const& fixture, ExecuteViaHostOutput const& output, int64_t gasBefore)
{
    BOOST_CHECK_EQUAL(
        static_cast<int>(output.evmcResult.status_code), static_cast<int>(fixture.expected.status));
    bcos::bytes actual(output.evmcResult.output_data,
        output.evmcResult.output_data + output.evmcResult.output_size);
    BOOST_CHECK_MESSAGE(sameBytes(actual, fixture.expected.output),
        "output mismatch actual=0x" << bcos::toHex(actual) << " expected=0x"
                                    << bcos::toHex(fixture.expected.output));
    if (fixture.expected.gasUsed != 0)
    {
        int64_t const actualGas = gasBefore - output.evmcResult.gas_left;
        BOOST_CHECK_LE(
            std::abs(actualGas - fixture.expected.gasUsed), fixture.expected.gasUsedTolerance);
    }
}

}  // namespace bcos::evm::test::fixtures
