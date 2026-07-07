#include "bcos-evm/eth-eest-test/EestForkInference.h"
#include "bcos-evm/eth-eest-test/EestGranularCli.h"
#include "bcos-evm/eth-eest-test/EestGranularSlowFilter.h"
#include "bcos-evm/eth-eest-test/EestStateTestLoader.h"
#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/StateTestAssert.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace bcos::evm::reference_tests
{
namespace
{

static constexpr std::array<char const*, 3> kDefaultAssertLevels = {
    "transitional", "expectException", "stateRoot"};

struct RunnerConfig
{
    std::vector<ForkProfile> profiles;
    fs::path stateTestsRoot;
    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
};

bool passesNameFilter(std::string const& testName, std::optional<std::string> const& nameFilter)
{
    return !nameFilter || testName.find(*nameFilter) != std::string::npos;
}

// ── File-level GTest case (directory input) ──────────────────────────────

class EthEestStateFileTest : public testing::Test
{
    fs::path m_file;
    RunnerConfig* m_config;

public:
    EthEestStateFileTest(fs::path file, RunnerConfig* config) noexcept
      : m_file(std::move(file)), m_config(config)
    {}

    void TestBody() final
    {
        auto testCases = loadEestStateTestFile(m_file);
        for (auto const& tc : testCases)
        {
            for (auto const& run :
                resolveRunsForCase(tc, m_file, m_config->stateTestsRoot, m_config->profiles))
            {
                auto const subtests = tryListSubtests(tc, run.postForkKey);
                if (subtests.empty())
                {
                    continue;
                }

                EthMessageAdapter adapter(run.executionProfile, m_config->hashImpl, m_config->vm);
                for (auto const& st : subtests)
                {
                    SCOPED_TRACE(
                        std::string(evmc::to_string(run.executionProfile.revision.revision)) +
                        " d" + std::to_string(st.dataIndex) + "g" + std::to_string(st.gasIndex) +
                        "v" + std::to_string(st.valueIndex));

                    auto const expected = selectExpected(tc, st);
                    auto gasBefore =
                        tc.transaction.gasLimit.empty() ?
                            0 :
                            static_cast<int64_t>(
                                tc.transaction.gasLimit[static_cast<size_t>(st.gasIndex)]);
                    auto const result = task::syncWait(adapter.execute(tc, st));

                    ManifestEntry synthetic;
                    synthetic.evidenceId = tc.name + "@" + run.executionProfile.profileId;
                    synthetic.path = ExecutionPath::Reference;
                    synthetic.evidenceKind = EvidenceKind::ReferenceParity;
                    synthetic.assertLevels = {
                        kDefaultAssertLevels.begin(), kDefaultAssertLevels.end()};

                    auto const report = assertResult(synthetic, expected, result, gasBefore);
                    EXPECT_TRUE(report.passed) << report.message;
                }
            }
        }
    }

    static void register_one(std::string const& suite, fs::path const& file, RunnerConfig* config)
    {
        testing::RegisterTest(suite.c_str(), file.stem().string().c_str(), nullptr, nullptr,
            file.string().c_str(), 0,
            [file, config]() -> testing::Test* { return new EthEestStateFileTest(file, config); });
    }
};

// ── Subtest-level GTest case (single-file input) ─────────────────────────

class EthEestStateSubtest : public testing::Test
{
    StateTestCase m_testCase;
    StateSubtest m_subtest;
    ForkProfile m_profile;
    RunnerConfig* m_config;

public:
    EthEestStateSubtest(StateTestCase testCase, StateSubtest subtest, ForkProfile profile,
        RunnerConfig* config) noexcept
      : m_testCase(std::move(testCase)),
        m_subtest(std::move(subtest)),
        m_profile(std::move(profile)),
        m_config(config)
    {}

    void TestBody() final
    {
        auto const expected = selectExpected(m_testCase, m_subtest);
        auto gasBefore =
            m_testCase.transaction.gasLimit.empty() ?
                0 :
                static_cast<int64_t>(
                    m_testCase.transaction.gasLimit[static_cast<size_t>(m_subtest.gasIndex)]);

        EthMessageAdapter adapter(m_profile, m_config->hashImpl, m_config->vm);
        auto const result = task::syncWait(adapter.execute(m_testCase, m_subtest));

        ManifestEntry synthetic;
        synthetic.evidenceId = m_testCase.name;
        synthetic.path = ExecutionPath::Reference;
        synthetic.evidenceKind = EvidenceKind::ReferenceParity;
        synthetic.assertLevels = {kDefaultAssertLevels.begin(), kDefaultAssertLevels.end()};

        auto const report = assertResult(synthetic, expected, result, gasBefore);
        EXPECT_TRUE(report.passed) << report.message;
    }

    static void register_one(StateTestCase const& testCase, StateSubtest const& subtest,
        ForkProfile const& profile, RunnerConfig* config, std::string const& suite,
        std::string const& testName)
    {
        testing::RegisterTest(suite.c_str(), testName.c_str(), nullptr, nullptr, __FILE__, __LINE__,
            [testCase, subtest, profile, config]() -> testing::Test* {
                return new EthEestStateSubtest(testCase, subtest, profile, config);
            });
    }
};

// ── Discovery ─────────────────────────────────────────────────────────────

void registerFilesFromDirectory(
    fs::path const& root, RunnerConfig* config, std::optional<std::string> const& nameFilter)
{
    std::vector<fs::path> testFiles;
    for (auto const& entry :
        fs::recursive_directory_iterator{root, fs::directory_options::skip_permission_denied})
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json" &&
            entry.path().filename() != "index.json")
        {
            testFiles.push_back(entry.path());
        }
    }
    std::ranges::sort(testFiles);
    for (auto const& p : testFiles)
    {
        auto const testName = p.stem().string();
        if (!passesNameFilter(testName, nameFilter))
        {
            continue;
        }
        EthEestStateFileTest::register_one(fs::relative(p, root).parent_path().string(), p, config);
    }
}

void registerSubtestsFromFile(
    fs::path const& file, RunnerConfig* config, std::optional<std::string> const& nameFilter)
{
    auto testCases = loadEestStateTestFile(file);
    for (auto const& tc : testCases)
    {
        for (auto const& run :
            resolveRunsForCase(tc, file, config->stateTestsRoot, config->profiles))
        {
            auto const subtests = tryListSubtests(tc, run.postForkKey);
            for (auto const& st : subtests)
            {
                auto testName = tc.name + "/" + run.postForkKey + "/d" +
                                std::to_string(st.dataIndex) + "g" + std::to_string(st.gasIndex) +
                                "v" + std::to_string(st.valueIndex);
                if (!passesNameFilter(testName, nameFilter))
                {
                    continue;
                }
                EthEestStateSubtest::register_one(
                    tc, st, run.executionProfile, config, file.string(), testName);
            }
        }
    }
}

}  // namespace
}  // namespace bcos::evm::reference_tests


int main(int argc, char** argv)
{
    using namespace bcos::evm::reference_tests;

    try
    {
        testing::FLAGS_gtest_filter = std::string(kEestGranularDefaultGtestFilter);
        testing::InitGoogleTest(&argc, argv);

        auto opts = parseEestGranularCliRemaining(argc, argv);
        if (opts.paths.empty())
        {
            std::cerr << "Usage: " << argv[0]
                      << " <path> [<path>...] [-k SUBSTR] [--fork-profiles IDS]\n"
                      << "       [--gtest_filter=...]   # standard GTest flags\n";
            return 1;
        }

        RunnerConfig config;
        config.profiles = buildRunnerConfig(opts.profileIds);

        auto eestRoot = resolveEestRoot();
        ensureEestFixturesExtracted(eestRoot);
        config.stateTestsRoot = eestRoot / "fixtures" / "state_tests";

        for (auto const& root : opts.paths)
        {
            if (is_directory(root))
            {
                registerFilesFromDirectory(root, &config, opts.nameFilter);
            }
            else if (is_regular_file(root))
            {
                registerSubtestsFromFile(root, &config, opts.nameFilter);
            }
            else
            {
                std::cerr << "Error: '" << root << "' is not a valid directory or file\n";
                return 1;
            }
        }

        return RUN_ALL_TESTS();
    }
    catch (std::exception const& ex)
    {
        std::cerr << "EthEestStateGranular error: " << ex.what() << '\n';
        return 1;
    }
}
