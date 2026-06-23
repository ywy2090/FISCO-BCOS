#include "bcos-evm/specs-tests/StateTestAssert.h"

#include "bcos-utilities/DataConvertUtility.h"
#include <algorithm>
#include <cstring>

namespace bcos::evm::reference_tests
{
namespace
{

bool containsAssertLevel(ManifestEntry const& entry, char const* level)
{
    return std::find(entry.assertLevels.begin(), entry.assertLevels.end(), level) !=
           entry.assertLevels.end();
}

bool bytes32Equal(evmc_bytes32 const& lhs, evmc_bytes32 const& rhs)
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

std::string formatBytes32(evmc_bytes32 const& value)
{
    bcos::bytes bytes(value.bytes, value.bytes + sizeof(value.bytes));
    return bcos::toHex(bytes);
}

}  // namespace

AssertReport assertResult(ManifestEntry const& entry, ExpectedPostState const& expected,
    ExecutionResult const& actual, int64_t gasBefore)
{
    static_cast<void>(gasBefore);

    AssertReport report;
    report.passed = true;

    if (containsAssertLevel(entry, "expectException"))
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
        if (!actual.stateRoot.has_value())
        {
            report.passed = false;
            report.message = "Missing computed stateRoot";
            return report;
        }
        if (!bytes32Equal(*actual.stateRoot, *expected.stateRoot))
        {
            report.passed = false;
            report.message = "stateRoot mismatch got 0x" + formatBytes32(*actual.stateRoot) +
                             " want 0x" + formatBytes32(*expected.stateRoot);
            return report;
        }
    }

    if (containsAssertLevel(entry, "logsHash") && expected.logsHash.has_value())
    {
        if (!actual.logsHash.has_value())
        {
            report.passed = false;
            report.message = "Missing computed logsHash";
            return report;
        }
        if (!bytes32Equal(*actual.logsHash, *expected.logsHash))
        {
            report.passed = false;
            report.message = "logsHash mismatch got 0x" + formatBytes32(*actual.logsHash) +
                             " want 0x" + formatBytes32(*expected.logsHash);
            return report;
        }
    }

    return report;
}

}  // namespace bcos::evm::reference_tests
