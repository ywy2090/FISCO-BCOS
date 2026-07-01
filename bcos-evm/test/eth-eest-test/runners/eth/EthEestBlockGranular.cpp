#include "bcos-evm/eth-eest-test/EestStateTestLoader.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

class EestBlockFileTest : public testing::Test
{
    fs::path m_file;

public:
    explicit EestBlockFileTest(fs::path file) : m_file(std::move(file)) {}

    void TestBody() final
    {
        // Validate JSON fixture is loadable, has pre-state and at least one block.
        // Full state-root validation is performed by EthEestBlockchainRunner.
        // This test provides gtest-filterable per-file smoke coverage.
        boost::property_tree::ptree root;
        boost::property_tree::read_json(m_file.string(), root);

        for (auto const& [testName, testTree] : root)
        {
            ASSERT_TRUE(testTree.count("pre")) << testName << ": missing pre-state";
            ASSERT_TRUE(testTree.count("genesisBlockHeader"))
                << testName << ": missing genesis block header";

            // Verify at least one supported format exists
            bool hasBlocks = testTree.count("blocks") > 0;
            bool hasPayloads = testTree.count("engineNewPayloads") > 0;
            EXPECT_TRUE(hasBlocks || hasPayloads)
                << testName << ": no blocks or engine payloads found";
        }
    }

    static void register_one(std::string const& suite, fs::path const& file)
    {
        testing::RegisterTest(suite.c_str(), file.stem().string().c_str(), nullptr, nullptr,
            file.string().c_str(), 0,
            [file]() -> testing::Test* { return new EestBlockFileTest(file); });
    }
};

}  // namespace

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

    if (is_directory(root))
    {
        std::vector<fs::path> files;
        for (auto& entry :
            fs::recursive_directory_iterator{root, fs::directory_options::skip_permission_denied})
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                files.push_back(entry.path());
        std::ranges::sort(files);
        for (auto& f : files)
            EestBlockFileTest::register_one(fs::relative(f, root).parent_path().string(), f);
    }

    return RUN_ALL_TESTS();
}
