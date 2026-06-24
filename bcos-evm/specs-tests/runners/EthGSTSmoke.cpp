#include "bcos-evm/specs-tests/EthReferenceBridgeAdapter.h"
#include "bcos-evm/specs-tests/ForkProfileRegistry.h"
#include "bcos-evm/specs-tests/GeneralStateTestLoader.h"
#include "bcos-evm/specs-tests/ManifestLoader.h"
#include "bcos-evm/specs-tests/StateTestAssert.h"
#include "bcos-evm/specs-tests/StateTestMatcher.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <iostream>
#include <string>
#include <string_view>

namespace bcos::evm::reference_tests
{
namespace
{

struct Options
{
    std::filesystem::path manifestPath;
    std::filesystem::path ethereumTestsRoot;
    std::filesystem::path expectationsPath;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    options.ethereumTestsRoot = resolveEthereumTestsRoot();
#ifdef SPECS_TESTS_MANIFEST_DIR
    options.expectationsPath =
        std::filesystem::path(SPECS_TESTS_MANIFEST_DIR) / "expectations.json";
#else
    options.expectationsPath = std::filesystem::path("manifests/expectations.json");
#endif

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--manifest" && i + 1 < argc)
        {
            options.manifestPath = argv[++i];
        }
        else if (arg == "--ethereum-tests-root" && i + 1 < argc)
        {
            options.ethereumTestsRoot = argv[++i];
        }
        else if (arg == "--expectations" && i + 1 < argc)
        {
            options.expectationsPath = argv[++i];
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    if (options.manifestPath.empty())
    {
        throw std::runtime_error("Missing required --manifest argument");
    }

    return options;
}

}  // namespace
}  // namespace bcos::evm::reference_tests

int main(int argc, char** argv)
{
    using namespace bcos::evm::reference_tests;

    try
    {
        auto const options = parseOptions(argc, argv);
        ensureGeneralStateTestsExtracted(options.ethereumTestsRoot);

        auto const entries = loadManifest(options.manifestPath);
        StateTestMatcher matcher(options.expectationsPath);

        bcos::crypto::Keccak256 hashImpl;
        evmc::VM vm{evmc_create_evmone()};

        int failures = 0;
        int executed = 0;

        for (auto const& entry : entries)
        {
            auto const match = matcher.decide(entry.casePath, entry.path);
            if (match.kind == MatchDecision::Kind::Skip)
            {
                std::cout << "SKIP " << entry.evidenceId << " (" << match.reason.value_or("skip")
                          << ")\n";
                continue;
            }

            auto const profile =
                ForkProfileRegistry::instance().findByProfileId(entry.forkProfileId);
            if (!profile.has_value())
            {
                std::cerr << "FAIL " << entry.evidenceId << ": unknown forkProfileId\n";
                ++failures;
                continue;
            }

            EthReferenceBridgeAdapter adapter(*profile, hashImpl, vm);
            auto const gstPath = options.ethereumTestsRoot / entry.casePath;
            auto const testCase = loadGeneralStateTest(
                gstPath, entry.variantKey ? std::optional<std::string_view>{*entry.variantKey} :
                                            std::nullopt);
            auto const subtests =
                listSubtests(testCase, entry.postFork.value_or(profile->upstreamForkName));

            for (auto const& subtest : subtests)
            {
                auto const expected = selectExpected(testCase, subtest);
                auto const result = bcos::task::syncWait(adapter.execute(testCase, subtest));
                auto const report = assertResult(entry, expected, result,
                    testCase.transaction.gasLimit.empty() ?
                        0 :
                        static_cast<int64_t>(
                            testCase.transaction.gasLimit[static_cast<size_t>(subtest.gasIndex)]));

                ++executed;
                if (!report.passed)
                {
                    std::cerr << "FAIL " << entry.evidenceId << " subtest fork=" << subtest.fork
                              << " d" << subtest.dataIndex << "g" << subtest.gasIndex << "v"
                              << subtest.valueIndex << ": " << report.message << '\n';
                    ++failures;
                }
                else
                {
                    std::cout << "PASS " << entry.evidenceId << " fork=" << subtest.fork << '\n';
                }
            }
        }

        if (executed == 0)
        {
            std::cerr << "No GST subtests executed\n";
            return 1;
        }

        if (failures != 0)
        {
            std::cerr << failures << " failure(s)\n";
            return 1;
        }

        std::cout << "All " << executed << " subtest(s) passed\n";
        return 0;
    }
    catch (std::exception const& ex)
    {
        std::cerr << "EthGSTSmoke error: " << ex.what() << '\n';
        return 1;
    }
}
