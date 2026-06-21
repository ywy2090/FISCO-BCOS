#define BOOST_TEST_MODULE StateTestAssertTest
#include "bcos-evm/evm-reference-tests/StateTestAssert.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(transitional_success_when_no_expect_exception)
{
    ManifestEntry entry;
    entry.assertLevels = {"transitional"};

    ExpectedPostState expected;
    ExecutionResult actual;
    actual.status = EVMC_SUCCESS;

    auto const report = assertResult(entry, expected, actual, 100000);
    BOOST_CHECK(report.passed);
}

BOOST_AUTO_TEST_CASE(transitional_fails_on_unexpected_revert)
{
    ManifestEntry entry;
    entry.assertLevels = {"transitional"};

    ExpectedPostState expected;
    ExecutionResult actual;
    actual.status = EVMC_REVERT;

    auto const report = assertResult(entry, expected, actual, 100000);
    BOOST_CHECK(!report.passed);
}

}  // namespace bcos::evm::reference_tests
