#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include "bcos-evm/eth-eest-test/EestStateTestLoader.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "helpers/BlockchainRunCore.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

namespace bcos::evm::reference_tests
{
namespace
{

struct RunnerConfig
{
    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
};

std::string joinFailures(std::vector<std::string> const& failures)
{
    std::ostringstream oss;
    for (size_t i = 0; i < failures.size(); ++i)
    {
        if (i > 0)
            oss << "; ";
        oss << failures[i];
    }
    return oss.str();
}

class EestBlockFileTest : public testing::Test
{
    fs::path m_file;
    RunnerConfig* m_config;

public:
    EestBlockFileTest(fs::path file, RunnerConfig* config) noexcept
      : m_file(std::move(file)), m_config(config)
    {}

    void TestBody() final
    {
        pt::ptree root;
        try
        {
            pt::read_json(m_file.string(), root);
        }
        catch (std::exception const& e)
        {
            FAIL() << "parse error: " << e.what();
        }

        auto tests = loadBlockchainTests(root);
        if (tests.empty())
        {
            GTEST_SKIP() << "no supported tests";
        }

        auto const forkFilter = inferBlockchainForkFromPath(m_file);
        size_t ran = 0;
        for (auto const& test : tests)
        {
            if (!forkFilter.empty() && test.network != forkFilter)
                continue;
            if (!ForkProfileRegistry::instance().findByUpstreamFork(test.network).has_value())
                continue;
            ++ran;
            SCOPED_TRACE(test.name);
            auto failures = runBlockchainTest(test, m_config->vm, m_config->hashImpl);
            EXPECT_TRUE(failures.empty()) << test.name << ": " << joinFailures(failures);
        }
        if (ran == 0)
        {
            GTEST_SKIP() << "no tests for fork " << forkFilter;
        }
    }

    static void register_one(std::string const& suite, fs::path const& file, RunnerConfig* config)
    {
        testing::RegisterTest(suite.c_str(), file.stem().string().c_str(), nullptr, nullptr,
            file.string().c_str(), 0,
            [file, config]() -> testing::Test* { return new EestBlockFileTest(file, config); });
    }
};

}  // namespace
}  // namespace bcos::evm::reference_tests

int main(int argc, char** argv)
{
    using namespace bcos::evm::reference_tests;

    testing::InitGoogleTest(&argc, argv);
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <path>\n";
        return 1;
    }

    fs::path root(argv[1]);
    ensureEestFixturesExtracted(resolveEestRoot());

    RunnerConfig config;

    if (is_directory(root))
    {
        std::vector<fs::path> files;
        for (auto& entry :
            fs::recursive_directory_iterator{root, fs::directory_options::skip_permission_denied})
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                files.push_back(entry.path());
        std::ranges::sort(files);
        for (auto& f : files)
            EestBlockFileTest::register_one(
                fs::relative(f, root).parent_path().string(), f, &config);
    }

    return RUN_ALL_TESTS();
}
