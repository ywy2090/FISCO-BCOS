#include "bcos-evm/eth-eest-test/EestStateTestLoader.h"
#include "bcos-evm/eth-eest-test/EthReferenceBridgeAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/ManifestLoader.h"
#include "bcos-evm/eth-eest-test/StateTestAssert.h"
#include "bcos-evm/eth-eest-test/StateTestMatcher.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{
namespace
{

struct Options
{
    std::filesystem::path manifestPath;
    std::filesystem::path eestRoot;
    std::filesystem::path expectationsPath;
    std::optional<size_t> limit;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    options.eestRoot = resolveEestRoot();
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
        else if (arg == "--eest-root" && i + 1 < argc)
        {
            options.eestRoot = argv[++i];
        }
        else if (arg == "--expectations" && i + 1 < argc)
        {
            options.expectationsPath = argv[++i];
        }
        else if (arg == "--limit" && i + 1 < argc)
        {
            options.limit = static_cast<size_t>(std::stoull(argv[++i]));
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

std::vector<std::filesystem::path> resolveCasePaths(
    std::filesystem::path const& eestRoot, ManifestEntry const& entry)
{
    auto const target = eestRoot / entry.casePath;
    if (std::filesystem::is_directory(target))
    {
        return listEestStateTestFiles(eestRoot, entry.casePath);
    }
    return {target};
}

bool runEntry(ManifestEntry const& entry, Options const& options, StateTestMatcher const& matcher,
    bcos::crypto::Keccak256& hashImpl, evmc::VM& vm, int& failures, int& executed)
{
    if (entry.sourceSuite != "eest")
    {
        std::cerr << "FAIL " << entry.evidenceId << ": sourceSuite must be eest\n";
        ++failures;
        return false;
    }

    auto const match = matcher.decide(entry.casePath, entry.path);
    if (match.kind == MatchDecision::Kind::Skip)
    {
        std::cout << "SKIP " << entry.evidenceId << " (" << match.reason.value_or("skip") << ")\n";
        return true;
    }

    auto const profile = ForkProfileRegistry::instance().findByProfileId(entry.forkProfileId);
    if (!profile.has_value())
    {
        std::cerr << "FAIL " << entry.evidenceId << ": unknown forkProfileId\n";
        ++failures;
        return false;
    }

    EthReferenceBridgeAdapter adapter(*profile, hashImpl, vm);
    for (auto const& jsonPath : resolveCasePaths(options.eestRoot, entry))
    {
        std::vector<StateTestCase> cases;
        try
        {
            if (entry.variantKey.has_value())
            {
                cases.push_back(loadEestStateTest(jsonPath, *entry.variantKey));
            }
            else
            {
                cases = loadEestStateTestFile(jsonPath);
            }
        }
        catch (std::exception const& ex)
        {
            std::cerr << "SKIP " << entry.evidenceId << " " << jsonPath << " (" << ex.what()
                      << ")\n";
            continue;
        }

        for (auto const& testCase : cases)
        {
            auto const subtests =
                tryListSubtests(testCase, entry.postFork.value_or(profile->upstreamForkName));
            if (subtests.empty())
            {
                continue;
            }

            for (auto const& subtest : subtests)
            {
                if (options.limit.has_value() && executed >= static_cast<int>(*options.limit))
                {
                    return true;
                }
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
                    std::cerr << "FAIL " << entry.evidenceId << " fork=" << subtest.fork << ": "
                              << report.message << '\n';
                    ++failures;
                }
                else
                {
                    std::cout << "PASS " << entry.evidenceId << " fork=" << subtest.fork << '\n';
                }
            }
        }
    }
    return true;
}

}  // namespace
}  // namespace bcos::evm::reference_tests

int main(int argc, char** argv)
{
    using namespace bcos::evm::reference_tests;

    try
    {
        auto const options = parseOptions(argc, argv);
        ensureEestFixturesExtracted(options.eestRoot);

        auto const entries = loadManifest(options.manifestPath);
        StateTestMatcher matcher(options.expectationsPath);

        bcos::crypto::Keccak256 hashImpl;
        evmc::VM vm{evmc_create_evmone()};

        int failures = 0;
        int executed = 0;

        for (auto const& entry : entries)
        {
            runEntry(entry, options, matcher, hashImpl, vm, failures, executed);
        }

        if (executed == 0)
        {
            std::cerr << "No EEST state subtests executed\n";
            return 1;
        }

        if (failures != 0)
        {
            std::cerr << failures << " failure(s)\n";
            return 1;
        }

        std::cout << "All " << executed << " EEST state subtest(s) passed\n";
        return 0;
    }
    catch (std::exception const& ex)
    {
        std::cerr << "EthExecutionSpecStateTests error: " << ex.what() << '\n';
        return 1;
    }
}
