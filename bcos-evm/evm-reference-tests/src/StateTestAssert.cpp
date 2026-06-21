#include "bcos-evm/evm-reference-tests/StateTestAssert.h"

#include <algorithm>

namespace bcos::evm::reference_tests
{
namespace
{

bool containsAssertLevel(ManifestEntry const& entry, char const* level)
{
    return std::find(entry.assertLevels.begin(), entry.assertLevels.end(), level) !=
           entry.assertLevels.end();
}

}  // namespace

AssertReport assertResult(ManifestEntry const& entry, ExpectedPostState const& expected,
    ExecutionResult const& actual, int64_t gasBefore)
{
    static_cast<void>(gasBefore);

    AssertReport report;
    report.passed = true;

    if (containsAssertLevel(entry, "transitional"))
    {
        if (expected.expectException.has_value())
        {
            if (actual.status == EVMC_SUCCESS)
            {
                report.passed = false;
                report.message = "Expected exception '" + *expected.expectException +
                                 "' but execution succeeded";
                return report;
            }
        }
        else if (actual.status != EVMC_SUCCESS)
        {
            report.passed = false;
            report.message = "Expected success but got status " +
                             std::to_string(static_cast<int>(actual.status));
            return report;
        }
    }

    if (containsAssertLevel(entry, "stateRoot") && expected.stateRoot.has_value())
    {
        report.passed = false;
        report.message = "stateRoot assertion not implemented in P1 transitional mode";
        return report;
    }

    if (containsAssertLevel(entry, "logsHash") && expected.logsHash.has_value())
    {
        report.passed = false;
        report.message = "logsHash assertion not implemented in P1 transitional mode";
        return report;
    }

    return report;
}

}  // namespace bcos::evm::reference_tests
