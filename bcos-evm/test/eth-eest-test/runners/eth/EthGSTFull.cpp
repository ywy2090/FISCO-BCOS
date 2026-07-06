#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/StateTestAssert.h"
#include "bcos-evm/eth-eest-test/StateTestMatcher.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{
namespace
{

struct Options
{
    std::filesystem::path ethereumTestsRoot;
    std::filesystem::path expectationsPath;
    std::vector<std::string> forkProfileIds{"eth-cancun", "eth-prague", "eth-osaka"};
    std::optional<size_t> limit;
    std::optional<std::string> casePathPrefix;
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
        if (arg == "--ethereum-tests-root" && i + 1 < argc)
        {
            options.ethereumTestsRoot = argv[++i];
        }
        else if (arg == "--expectations" && i + 1 < argc)
        {
            options.expectationsPath = argv[++i];
        }
        else if (arg == "--fork-profiles" && i + 1 < argc)
        {
            options.forkProfileIds.clear();
            std::string_view list(argv[++i]);
            while (!list.empty())
            {
                auto const comma = list.find(',');
                auto const token = list.substr(0, comma);
                if (!token.empty())
                {
                    options.forkProfileIds.emplace_back(token);
                }
                if (comma == std::string_view::npos)
                {
                    break;
                }
                list.remove_prefix(comma + 1);
            }
        }
        else if (arg == "--limit" && i + 1 < argc)
        {
            options.limit = static_cast<size_t>(std::stoull(argv[++i]));
        }
        else if (arg == "--case-prefix" && i + 1 < argc)
        {
            options.casePathPrefix = argv[++i];
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    if (options.forkProfileIds.empty())
    {
        throw std::runtime_error("At least one fork profile is required");
    }

    return options;
}

std::optional<std::string> resolvePostFork(ForkProfile const& profile)
{
    if (profile.upstreamForkName == "Osaka")
    {
        return std::string{"Prague"};
    }
    return std::nullopt;
}

ManifestEntry makeSyntheticEntry(std::string const& evidenceId, ForkProfile const& profile)
{
    ManifestEntry entry;
    entry.evidenceId = evidenceId;
    entry.sourceSuite = "ethereum-tests";
    entry.forkProfileId = profile.profileId;
    entry.path = ExecutionPath::Reference;
    entry.evidenceKind = EvidenceKind::ReferenceParity;
    entry.capabilityRowIds = {"eip2929-runtime-warm"};
    entry.assertLevels = {"transitional", "expectException"};
    if (auto const fallback = resolvePostFork(profile))
    {
        entry.postFork = fallback;
    }
    return entry;
}

bool runSubtest(ForkProfile const& profile, EthMessageAdapter& adapter,
    StateTestCase const& testCase, StateSubtest const& subtest, ManifestEntry const& entry,
    int& failures)
{
    static_cast<void>(profile);
    auto const expected = selectExpected(testCase, subtest);
    auto const result = bcos::task::syncWait(adapter.execute(testCase, subtest));
    auto const gasBefore =
        testCase.transaction.gasLimit.empty() ?
            0 :
            static_cast<int64_t>(
                testCase.transaction.gasLimit[static_cast<size_t>(subtest.gasIndex)]);
    auto const report = assertResult(entry, expected, result, gasBefore);
    if (!report.passed)
    {
        std::cerr << "FAIL " << entry.evidenceId << " fork=" << subtest.fork << " d"
                  << subtest.dataIndex << "g" << subtest.gasIndex << "v" << subtest.valueIndex
                  << ": " << report.message << '\n';
        ++failures;
        return false;
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
        ensureGeneralStateTestsExtracted(options.ethereumTestsRoot);

        StateTestMatcher matcher(options.expectationsPath);
        bcos::crypto::Keccak256 hashImpl;
        evmc::VM vm{evmc_create_evmone()};

        auto const gstRoot = options.ethereumTestsRoot / "GeneralStateTests";
        if (!std::filesystem::exists(gstRoot))
        {
            throw std::runtime_error("GeneralStateTests directory not found: " + gstRoot.string());
        }

        int failures = 0;
        int executed = 0;

        for (auto const& jsonPath : std::filesystem::recursive_directory_iterator(
                 gstRoot, std::filesystem::directory_options::skip_permission_denied))
        {
            if (!jsonPath.is_regular_file() || jsonPath.path().extension() != ".json")
            {
                continue;
            }
            if (options.limit.has_value() && executed >= static_cast<int>(*options.limit))
            {
                break;
            }

            auto const caseId = generalStateTestCaseId(options.ethereumTestsRoot, jsonPath.path());
            if (options.casePathPrefix.has_value() &&
                caseId.find(*options.casePathPrefix) == std::string::npos)
            {
                continue;
            }
            auto const match = matcher.decide(caseId, ExecutionPath::Reference);
            if (match.kind == MatchDecision::Kind::Skip)
            {
                continue;
            }

            std::vector<StateTestCase> cases;
            try
            {
                cases = loadGeneralStateTestFile(jsonPath.path());
            }
            catch (std::exception const& ex)
            {
                std::cerr << "SKIP " << caseId << " (load error: " << ex.what() << ")\n";
                continue;
            }

            for (auto const& profileId : options.forkProfileIds)
            {
                auto const profile = ForkProfileRegistry::instance().findByProfileId(profileId);
                if (!profile.has_value())
                {
                    std::cerr << "FAIL unknown fork profile " << profileId << '\n';
                    ++failures;
                    continue;
                }

                auto const entry = makeSyntheticEntry(caseId + "@" + profileId, *profile);
                auto const executionProfile =
                    ForkProfileRegistry::instance().resolveExecutionProfile(
                        *profile, entry.postFork);
                EthMessageAdapter adapter(executionProfile, hashImpl, vm);

                for (auto const& testCase : cases)
                {
                    auto const postFork = entry.postFork.value_or(profile->upstreamForkName);
                    std::vector<StateSubtest> subtests;
                    try
                    {
                        subtests = listSubtests(testCase, postFork);
                    }
                    catch (std::exception const&)
                    {
                        continue;
                    }

                    for (auto const& subtest : subtests)
                    {
                        if (options.limit.has_value() &&
                            executed >= static_cast<int>(*options.limit))
                        {
                            break;
                        }
                        ++executed;
                        runSubtest(*profile, adapter, testCase, subtest, entry, failures);
                    }
                }
            }
        }

        if (executed == 0)
        {
            std::cerr << "No GST subtests executed\n";
            return 1;
        }

        std::cout << "Executed " << executed << " subtest(s), " << failures << " failure(s)\n";
        return failures == 0 ? 0 : 1;
    }
    catch (std::exception const& ex)
    {
        std::cerr << "EthGSTFull error: " << ex.what() << '\n';
        return 1;
    }
}
