#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"

#include "bcos-crypto/hash/Keccak256.h"
#include "helpers/BlockTransition.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{

struct BlockTestFixture
{
    std::string name;
    bcos::evm::reference_tests::TestStateView preState;
    bcos::evm::state::BlockInfo blockInfo;
    std::vector<bcos::evm::state::Transaction> transactions;
    bcos::evm::reference_tests::GstPostStateView expectedPostState;
};

std::string normalizeHexAddressKey(std::string const& addrHex)
{
    if (addrHex.starts_with("0x") || addrHex.starts_with("0X"))
    {
        return addrHex;
    }
    return "0x" + addrHex;
}

// Load a simple block test JSON.
// Format mirrors a subset of evmone's BlockchainTest:
// { "pre": {...}, "blocks": [{ "transactions": [...], "expectedPost": {...} }] }
BlockTestFixture loadBlockTest(fs::path const& path)
{
    std::ifstream f{path};
    Json::Value j;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, f, &j, &errs))
    {
        throw std::runtime_error("Failed to parse block test JSON: " + path.string() + ": " + errs);
    }

    BlockTestFixture fixture;
    fixture.name = path.stem().string();

    // Pre-state
    if (j.isMember("pre"))
    {
        for (auto const& addrHex : j["pre"].getMemberNames())
        {
            auto const& accJson = j["pre"][addrHex];
            auto addr = bcos::evm::state::parseHexAddress(normalizeHexAddressKey(addrHex));
            bcos::evm::state::Account acc;
            acc.balance = bcos::fromBigQuantity(accJson.get("balance", "0x0").asString());
            acc.nonce = static_cast<uint64_t>(
                bcos::fromBigQuantity(accJson.get("nonce", "0x0").asString()));
            auto const codeHex = accJson.get("code", "0x").asString();
            if (!codeHex.empty() && codeHex != "0x")
                acc.code = bcos::fromHex(codeHex);
            fixture.preState.insertAccount(addr, std::move(acc));
        }
    }

    // First block's transactions and expected post
    if (j.isMember("blocks") && !j["blocks"].empty())
    {
        auto const& blk = j["blocks"][0];
        if (blk.isMember("transactions"))
        {
            for (auto const& txJson : blk["transactions"])
            {
                bcos::evm::state::Transaction tx;
                tx.from = bcos::evm::state::parseHexAddress(txJson.get("sender", "0x").asString());
                auto const toStr = txJson.get("to", "").asString();
                if (!toStr.empty())
                    tx.to = bcos::evm::state::parseHexAddress(toStr);
                tx.data = bcos::fromHex(txJson.get("data", "0x").asString());
                tx.value = bcos::fromBigQuantity(txJson.get("value", "0x0").asString());
                tx.gasLimit = static_cast<int64_t>(
                    bcos::fromBigQuantity(txJson.get("gasLimit", "0x0").asString()));
                tx.gasPrice = bcos::fromBigQuantity(txJson.get("gasPrice", "0x0").asString());
                tx.nonce = static_cast<uint64_t>(
                    bcos::fromBigQuantity(txJson.get("nonce", "0x0").asString()));
                fixture.transactions.push_back(std::move(tx));
            }
        }

        // Expected post-state
        if (blk.isMember("expectedPost"))
        {
            for (auto const& addrHex : blk["expectedPost"].getMemberNames())
            {
                auto const& accJson = blk["expectedPost"][addrHex];
                auto addr = bcos::evm::state::parseHexAddress(normalizeHexAddressKey(addrHex));
                bcos::evm::state::Account acc;
                acc.balance = bcos::fromBigQuantity(accJson.get("balance", "0x0").asString());
                acc.nonce = static_cast<uint64_t>(
                    bcos::fromBigQuantity(accJson.get("nonce", "0x0").asString()));
                fixture.expectedPostState.accounts.emplace_back(addr, std::move(acc));
            }
        }
    }

    return fixture;
}

class EthBlockTest : public testing::Test
{
    fs::path m_file;
    bcos::evm::reference_tests::ForkProfile m_profile;
    bcos::crypto::Keccak256 m_hashImpl;
    evmc::VM m_vm{evmc_create_evmone()};

public:
    EthBlockTest(fs::path file, bcos::evm::reference_tests::ForkProfile profile) noexcept
      : m_file(std::move(file)), m_profile(std::move(profile))
    {}

    void TestBody() final
    {
        try
        {
            runTest();
        }
        catch (std::exception const& ex)
        {
            FAIL() << "Exception: " << ex.what() << " in " << m_file;
        }
        catch (...)
        {
            FAIL() << "Unknown exception in " << m_file;
        }
    }

    void runTest()
    {
        auto fixture = loadBlockTest(m_file);

        // Set block info
        fixture.blockInfo.coinbase =
            bcos::evm::state::parseHexAddress("0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba");
        fixture.blockInfo.gasLimit = 30'000'000;
        fixture.blockInfo.number = 1;
        fixture.blockInfo.timestamp = 1;
        fixture.blockInfo.baseFee = 7;

        auto const result = bcos::evm::reference_tests::applyEthBlock(
            fixture.preState, fixture.transactions, fixture.blockInfo, m_profile, m_vm, m_hashImpl);

        // Build GstPostStateView from result.postState accounts
        bcos::evm::reference_tests::GstPostStateView actualView;
        for (auto const& [addr, acc] : result.postState.accounts())
        {
            actualView.accounts.emplace_back(addr, acc);
        }
        auto const actualRoot = bcos::evm::reference_tests::computeStateRoot(actualView);

        auto const expectedRoot =
            bcos::evm::reference_tests::computeStateRoot(fixture.expectedPostState);

        // Basic validation: execution completes without errors
        EXPECT_GT(result.gasUsed, 0) << "Block transition consumed no gas";
        EXPECT_GE(result.receipts.size(), 1u) << "Expected at least one receipt";

        // If expectedPostState is provided, compare state roots
        if (!fixture.expectedPostState.accounts.empty())
        {
            EXPECT_EQ(bcos::toHex(bcos::bytes(
                          actualRoot.bytes, actualRoot.bytes + sizeof(actualRoot.bytes))),
                bcos::toHex(bcos::bytes(
                    expectedRoot.bytes, expectedRoot.bytes + sizeof(expectedRoot.bytes))))
                << fixture.name;
        }
    }

    static void register_one(std::string const& suite, fs::path const& file,
        bcos::evm::reference_tests::ForkProfile const& profile)
    {
        testing::RegisterTest(suite.c_str(), file.stem().string().c_str(), nullptr, nullptr,
            file.string().c_str(), 0,
            [file, profile]() -> testing::Test* { return new EthBlockTest(file, profile); });
    }
};

}  // namespace

int main(int argc, char** argv)
{
    using namespace bcos::evm::reference_tests;

    testing::InitGoogleTest(&argc, argv);

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <fixtures-dir> [--fork-profile eth-cancun]\n";
        return 1;
    }

    fs::path fixturesDir(argv[1]);
    std::string profileId = "eth-cancun";
    for (int i = 2; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--fork-profile" && i + 1 < argc)
            profileId = argv[++i];
    }

    auto const profile = ForkProfileRegistry::instance().findByProfileId(profileId);
    if (!profile.has_value())
    {
        std::cerr << "Unknown fork profile: " << profileId << '\n';
        return 1;
    }

    if (!is_directory(fixturesDir))
    {
        std::cerr << "Error: '" << fixturesDir << "' is not a directory\n";
        return 1;
    }

    std::vector<fs::path> files;
    for (auto const& entry : fs::recursive_directory_iterator{
             fixturesDir, fs::directory_options::skip_permission_denied})
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            files.push_back(entry.path());
    }
    std::ranges::sort(files);

    for (auto const& f : files)
        EthBlockTest::register_one(
            fs::relative(f, fixturesDir).parent_path().string(), f, *profile);

    return RUN_ALL_TESTS();
}
