#define BOOST_TEST_MODULE StateTestAssertTest
#include "bcos-evm/eth-eest-test/StateTestAssert.h"
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

BOOST_AUTO_TEST_CASE(opstack_transitional_passes_7702_included_revert)
{
    ManifestEntry entry;
    entry.path = ExecutionPath::OpStackBaseline;
    entry.assertLevels = {"transitional", "expectException"};

    ExpectedPostState expected;
    ExecutionResult actual;
    actual.status = EVMC_REVERT;
    actual.authorizationListPresent = true;

    auto const report = assertResult(entry, expected, actual, 100000);
    BOOST_CHECK(report.passed);
}

BOOST_AUTO_TEST_CASE(opstack_transitional_fails_revert_without_auth_list)
{
    ManifestEntry entry;
    entry.path = ExecutionPath::OpStackBaseline;
    entry.assertLevels = {"transitional"};

    ExpectedPostState expected;
    ExecutionResult actual;
    actual.status = EVMC_REVERT;
    actual.authorizationListPresent = false;

    auto const report = assertResult(entry, expected, actual, 100000);
    BOOST_CHECK(!report.passed);
}

BOOST_AUTO_TEST_CASE(reference_path_still_fails_7702_revert)
{
    ManifestEntry entry;
    entry.path = ExecutionPath::Reference;
    entry.assertLevels = {"transitional"};

    ExpectedPostState expected;
    ExecutionResult actual;
    actual.status = EVMC_REVERT;
    actual.authorizationListPresent = true;

    auto const report = assertResult(entry, expected, actual, 100000);
    BOOST_CHECK(!report.passed);
}

}  // namespace bcos::evm::reference_tests
