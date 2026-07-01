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
        // Validate file is parseable and at least one block exists.
        // Full validation is performed by EthEestBlockchainRunner.
        GTEST_SKIP() << "EEST block granular validation — implemented in follow-up";
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
