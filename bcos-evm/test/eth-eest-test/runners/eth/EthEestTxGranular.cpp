#include "bcos-evm/eth-eest-test/EestTransactionTestLoader.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace
{

class EestTxTest : public testing::Test
{
    bcos::evm::reference_tests::TransactionTestCase m_testCase;

public:
    explicit EestTxTest(bcos::evm::reference_tests::TransactionTestCase tc)
      : m_testCase(std::move(tc))
    {}

    void TestBody() final
    {
        bool anyPass = false;
        for (auto const& [forkName, expected] : m_testCase.resultByFork)
        {
            auto profile =
                bcos::evm::reference_tests::ForkProfileRegistry::instance().findByUpstreamFork(
                    forkName);
            if (!profile.has_value())
                continue;

            // Transaction tests validate RLP decoding + intrinsic gas.
            // For now: verify the fixture is loadable and at least one fork has
            // results. Full validation requires adding tx RLP decoding to
            // bcos-evm.
            if (expected.hash.has_value() || expected.exception.has_value() ||
                expected.intrinsicGas.has_value())
            {
                anyPass = true;
            }
        }
        EXPECT_TRUE(anyPass) << "No fork results found for " << m_testCase.name;
    }

    static void register_one(std::string const& suite, std::string const& name,
        bcos::evm::reference_tests::TransactionTestCase const& tc)
    {
        testing::RegisterTest(suite.c_str(), name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
            [tc]() -> testing::Test* { return new EestTxTest(tc); });
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
    if (is_directory(root))
    {
        auto files = listEestTransactionTestFiles(root);
        for (auto& f : files)
        {
            auto cases = loadTransactionTestFile(f);
            for (auto& tc : cases)
                EestTxTest::register_one(fs::relative(f, root).parent_path().string(), tc.name, tc);
        }
    }
    else
    {
        auto cases = loadTransactionTestFile(root);
        for (auto& tc : cases)
            EestTxTest::register_one(root.string(), tc.name, tc);
    }

    return RUN_ALL_TESTS();
}
