#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/StateTestAssert.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace bcos::evm::reference_tests
{
namespace
{

struct RunnerConfig
{
    std::vector<ForkProfile> profiles;
    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
};

// ── File-level GTest case (directory input) ──────────────────────────────

class EthGstFileTest : public testing::Test
{
    fs::path m_file;
    RunnerConfig* m_config;

public:
    EthGstFileTest(fs::path file, RunnerConfig* config) noexcept
      : m_file(std::move(file)), m_config(config)
    {}

    void TestBody() final
    {
        auto testCases = loadGeneralStateTestFile(m_file);
        for (auto const& tc : testCases)
        {
            for (auto const& profile : m_config->profiles)
            {
                auto const postFork = profile.upstreamForkName;
                auto const subtests = tryListSubtests(tc, postFork);
                if (subtests.empty())
                    continue;

                EthMessageAdapter adapter(profile, m_config->hashImpl, m_config->vm);
                for (auto const& st : subtests)
                {
                    SCOPED_TRACE(std::string(evmc::to_string(profile.revision.revision)) + " d" +
                                 std::to_string(st.dataIndex) + "g" + std::to_string(st.gasIndex) +
                                 "v" + std::to_string(st.valueIndex));

                    auto const expected = selectExpected(tc, st);
                    auto gasBefore =
                        tc.transaction.gasLimit.empty() ?
                            0 :
                            static_cast<int64_t>(
                                tc.transaction.gasLimit[static_cast<size_t>(st.gasIndex)]);
                    auto const result = task::syncWait(adapter.execute(tc, st));

                    ManifestEntry synthetic;
                    synthetic.evidenceId = tc.name + "@" + profile.profileId;
                    synthetic.path = ExecutionPath::Reference;
                    synthetic.evidenceKind = EvidenceKind::ReferenceParity;
                    synthetic.assertLevels = {"transitional", "expectException"};

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
            [file, config]() -> testing::Test* { return new EthGstFileTest(file, config); });
    }
};

// ── Subtest-level GTest case (single-file input) ─────────────────────────

class EthGstSubtest : public testing::Test
{
    StateTestCase m_testCase;
    StateSubtest m_subtest;
    ForkProfile m_profile;
    RunnerConfig* m_config;

public:
    EthGstSubtest(StateTestCase testCase, StateSubtest subtest, ForkProfile profile,
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
        synthetic.assertLevels = {"transitional", "expectException"};

        auto const report = assertResult(synthetic, expected, result, gasBefore);
        EXPECT_TRUE(report.passed) << report.message;
    }

    static void register_one(StateTestCase const& testCase, StateSubtest const& subtest,
        ForkProfile const& profile, RunnerConfig* config, std::string const& suite,
        std::string const& testName)
    {
        testing::RegisterTest(suite.c_str(), testName.c_str(), nullptr, nullptr, __FILE__, __LINE__,
            [testCase, subtest, profile, config]() -> testing::Test* {
                return new EthGstSubtest(testCase, subtest, profile, config);
            });
    }
};

// ── Discovery ─────────────────────────────────────────────────────────────

void registerFilesFromDirectory(fs::path const& root, RunnerConfig* config)
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
        EthGstFileTest::register_one(fs::relative(p, root).parent_path().string(), p, config);
}

void registerSubtestsFromFile(fs::path const& file, RunnerConfig* config)
{
    std::ifstream f{file};
    auto testCases = loadGeneralStateTestFile(file);
    for (auto const& tc : testCases)
    {
        for (auto const& profile : config->profiles)
        {
            auto const subtests = tryListSubtests(tc, profile.upstreamForkName);
            for (auto const& st : subtests)
            {
                auto testName = tc.name + "/" + profile.upstreamForkName + "/d" +
                                std::to_string(st.dataIndex) + "g" + std::to_string(st.gasIndex) +
                                "v" + std::to_string(st.valueIndex);
                EthGstSubtest::register_one(tc, st, profile, config, file.string(), testName);
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
        testing::InitGoogleTest(&argc, argv);

        if (argc < 2)
        {
            std::cerr << "Usage: " << argv[0]
                      << " <path> [--fork-profiles eth-cancun,eth-prague,eth-osaka]\n";
            return 1;
        }

        fs::path root(argv[1]);
        RunnerConfig config;

        // Default profiles
        for (auto const& id : {"eth-cancun", "eth-prague", "eth-osaka"})
        {
            if (auto const p = ForkProfileRegistry::instance().findByProfileId(id))
            {
                if (std::ranges::none_of(config.profiles,
                        [&](auto const& fp) { return fp.profileId == p->profileId; }))
                    config.profiles.push_back(*p);
            }
        }

        // CLI: --fork-profiles
        for (int i = 2; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == "--fork-profiles" && i + 1 < argc)
            {
                config.profiles.clear();
                std::string_view list(argv[++i]);
                while (!list.empty())
                {
                    auto const comma = list.find(',');
                    auto const token = list.substr(0, comma);
                    if (!token.empty())
                    {
                        if (auto const p = ForkProfileRegistry::instance().findByProfileId(token))
                            config.profiles.push_back(*p);
                    }
                    if (comma == std::string_view::npos)
                        break;
                    list.remove_prefix(comma + 1);
                }
            }
        }

        ensureGeneralStateTestsExtracted(resolveEthereumTestsRoot());

        // Resolve path: first try as an absolute/relative path, then under GeneralStateTests
        auto gstRoot = resolveEthereumTestsRoot() / "GeneralStateTests";
        auto resolved = gstRoot / root;
        if (is_directory(resolved))
        {
            registerFilesFromDirectory(resolved, &config);
        }
        else if (is_regular_file(root))
        {
            registerSubtestsFromFile(root, &config);
        }
        else
        {
            std::cerr << "Error: '" << root << "' is not a valid path under " << gstRoot
                      << " nor a regular file\n";
            return 1;
        }

        return RUN_ALL_TESTS();
    }
    catch (std::exception const& ex)
    {
        std::cerr << "EthGSTGranular error: " << ex.what() << '\n';
        return 1;
    }
}
