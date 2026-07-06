#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"

#include "bcos-crypto/hash/Keccak256.h"
#include "helpers/BlockTransition.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{

/// ADR-028 proposed gate — entry failures geth rejects before inclusion.
inline bool isConsensusRejectedExitKind(bcos::evm::StateTransitionExitKind exitKind) noexcept
{
    switch (exitKind)
    {
    case bcos::evm::StateTransitionExitKind::RulesRejected:
    case bcos::evm::StateTransitionExitKind::GasAffordRejected:
    case bcos::evm::StateTransitionExitKind::IntrinsicRejected:
        return true;
    default:
        return false;
    }
}

struct ExpectedBlockReceipt
{
    evmc_status_code settlementStatus{EVMC_SUCCESS};
    bcos::protocol::TransactionStatus receiptStatus{bcos::protocol::TransactionStatus::None};
    bool includedTxVmError{false};
    bool consensusRejected{false};
    /// When true, ADR-028 TE gate should omit this tx from the block receipt list.
    bool adr028ExpectNoReceipt{false};
};

struct BlockTestFixture
{
    std::string name;
    std::string forkProfileId;
    bcos::evm::reference_tests::TestStateView preState;
    bcos::evm::state::BlockInfo blockInfo;
    std::vector<bcos::evm::reference_tests::GstTransactionTemplate> transactions;
    std::vector<ExpectedBlockReceipt> expectedReceipts;
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

evmc_status_code parseEvmcStatus(std::string const& name)
{
    if (name == "EVMC_SUCCESS")
        return EVMC_SUCCESS;
    if (name == "EVMC_REVERT")
        return EVMC_REVERT;
    if (name == "EVMC_OUT_OF_GAS")
        return EVMC_OUT_OF_GAS;
    if (name == "EVMC_FAILURE")
        return EVMC_FAILURE;
    if (name == "EVMC_UNDEFINED_INSTRUCTION")
        return EVMC_UNDEFINED_INSTRUCTION;
    if (name == "EVMC_INVALID_INSTRUCTION")
        return EVMC_INVALID_INSTRUCTION;
    throw std::runtime_error("Unknown EVMC status token: " + name);
}

bcos::protocol::TransactionStatus parseReceiptStatus(std::string const& name)
{
    if (name == "None")
        return bcos::protocol::TransactionStatus::None;
    if (name == "RevertInstruction")
        return bcos::protocol::TransactionStatus::RevertInstruction;
    if (name == "BadInstruction")
        return bcos::protocol::TransactionStatus::BadInstruction;
    if (name == "OutOfGas")
        return bcos::protocol::TransactionStatus::OutOfGas;
    if (name == "OutOfGasLimit")
        return bcos::protocol::TransactionStatus::OutOfGasLimit;
    if (name == "Unknown")
        return bcos::protocol::TransactionStatus::Unknown;
    throw std::runtime_error("Unknown receipt status token: " + name);
}

ExpectedBlockReceipt parseExpectedReceipt(Json::Value const& receiptJson)
{
    ExpectedBlockReceipt expected;
    expected.settlementStatus =
        parseEvmcStatus(receiptJson.get("settlementStatus", "EVMC_SUCCESS").asString());
    expected.receiptStatus =
        parseReceiptStatus(receiptJson.get("receiptStatus", "None").asString());
    expected.includedTxVmError = receiptJson.get("includedTxVmError", false).asBool();
    expected.consensusRejected = receiptJson.get("consensusRejected", false).asBool();
    expected.adr028ExpectNoReceipt = receiptJson.get("adr028ExpectNoReceipt", false).asBool();
    return expected;
}

// Load block-transition-v1 JSON:
// { "forkProfile": "eth-prague", "pre": {...}, "blocks": [{ "transactions": [...],
//   "expectedReceipts": [...], "expectedPost": {...} }] }
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

    if (!j.isMember("blocks"))
    {
        throw std::runtime_error(
            "Not a block-transition fixture (missing blocks): " + path.string());
    }

    BlockTestFixture fixture;
    fixture.name = path.stem().string();
    fixture.forkProfileId = j.get("forkProfile", "eth-cancun").asString();

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

    if (!j["blocks"].empty())
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
                fixture.transactions.push_back(
                    bcos::evm::reference_tests::gstTransactionTemplateFromSimple(tx));
            }
        }

        if (blk.isMember("expectedReceipts"))
        {
            for (auto const& receiptJson : blk["expectedReceipts"])
            {
                fixture.expectedReceipts.push_back(parseExpectedReceipt(receiptJson));
            }
        }

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

void assertReceiptMatches(ExpectedBlockReceipt const& expected,
    bcos::evm::reference_tests::TransactionReceipt const& actual, std::string_view caseName,
    size_t index)
{
    SCOPED_TRACE(std::string(caseName) + " receipt[" + std::to_string(index) + "]");

    EXPECT_EQ(actual.settlementStatus, expected.settlementStatus)
        << "settlement status_code mismatch";
    EXPECT_EQ(actual.receiptStatus, expected.receiptStatus) << "receipt TransactionStatus mismatch";
    EXPECT_EQ(actual.topLevelIncludedTxVmError, expected.includedTxVmError)
        << "topLevelIncludedTxVmError mismatch";
    EXPECT_EQ(isConsensusRejectedExitKind(actual.exitKind), expected.consensusRejected)
        << "consensusRejected / exitKind mismatch";

    if (expected.adr028ExpectNoReceipt)
    {
        // CURRENT_ORACLE (pre ADR-028): orchestration still materializes a receipt trace payload.
        // GETH_ORACLE / ADR-028: TE Finalize returns nullptr — flip receipt-count assert then.
        EXPECT_TRUE(isConsensusRejectedExitKind(actual.exitKind))
            << "ADR-028 oracle expects IntrinsicRejected/GasAffordRejected/RulesRejected exitKind";
    }
    else if (expected.consensusRejected)
    {
        EXPECT_TRUE(isConsensusRejectedExitKind(actual.exitKind));
    }
    else
    {
        EXPECT_FALSE(isConsensusRejectedExitKind(actual.exitKind))
            << "included execution should not be consensus-rejected";
    }
}

class EthBlockTest : public testing::Test
{
    fs::path m_file;
    std::string m_defaultProfileId;
    bcos::crypto::Keccak256 m_hashImpl;
    evmc::VM m_vm{evmc_create_evmone()};

public:
    EthBlockTest(fs::path file, std::string defaultProfileId) noexcept
      : m_file(std::move(file)), m_defaultProfileId(std::move(defaultProfileId))
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

        auto const profileId =
            fixture.forkProfileId.empty() ? m_defaultProfileId : fixture.forkProfileId;
        auto const profile =
            bcos::evm::reference_tests::ForkProfileRegistry::instance().findByProfileId(profileId);
        ASSERT_TRUE(profile.has_value()) << "Unknown fork profile: " << profileId;

        fixture.blockInfo.coinbase =
            bcos::evm::state::parseHexAddress("0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba");
        fixture.blockInfo.gasLimit = 30'000'000;
        fixture.blockInfo.number = 1;
        fixture.blockInfo.timestamp = 1;
        fixture.blockInfo.baseFee = 7;

        ASSERT_FALSE(fixture.transactions.empty()) << "Block fixture must include transactions";

        auto const result = bcos::evm::reference_tests::applyEthBlock(
            fixture.preState, fixture.transactions, fixture.blockInfo, *profile, m_vm, m_hashImpl);

        bool const allConsensusRejected =
            !fixture.expectedReceipts.empty() &&
            std::all_of(fixture.expectedReceipts.begin(), fixture.expectedReceipts.end(),
                [](ExpectedBlockReceipt const& r) { return r.consensusRejected; });
        if (!allConsensusRejected)
        {
            EXPECT_GT(result.gasUsed, 0) << "Block transition consumed no gas";
        }
        EXPECT_EQ(result.receipts.size(), fixture.transactions.size())
            << "CURRENT_ORACLE: one receipt per tx until ADR-028 TE nullptr gate";

        if (!fixture.expectedReceipts.empty())
        {
            ASSERT_EQ(result.receipts.size(), fixture.expectedReceipts.size())
                << "expectedReceipts length must match transaction count";
            for (size_t i = 0; i < fixture.expectedReceipts.size(); ++i)
            {
                assertReceiptMatches(
                    fixture.expectedReceipts[i], result.receipts[i], fixture.name, i);
            }
        }
        else
        {
            EXPECT_GE(result.receipts.size(), 1u) << "Expected at least one receipt";
        }

        bcos::evm::reference_tests::GstPostStateView actualView;
        for (auto const& [addr, acc] : result.postState.accounts())
        {
            actualView.accounts.emplace_back(addr, acc);
        }
        auto const actualRoot = bcos::evm::reference_tests::computeStateRoot(actualView);
        auto const expectedRoot =
            bcos::evm::reference_tests::computeStateRoot(fixture.expectedPostState);

        if (!fixture.expectedPostState.accounts.empty())
        {
            EXPECT_EQ(bcos::toHex(bcos::bytes(
                          actualRoot.bytes, actualRoot.bytes + sizeof(actualRoot.bytes))),
                bcos::toHex(bcos::bytes(
                    expectedRoot.bytes, expectedRoot.bytes + sizeof(expectedRoot.bytes))))
                << fixture.name;
        }
    }

    static void register_one(
        std::string const& suite, fs::path const& file, std::string const& defaultProfileId)
    {
        testing::RegisterTest(suite.c_str(), file.stem().string().c_str(), nullptr, nullptr,
            file.string().c_str(), 0, [file, defaultProfileId]() -> testing::Test* {
                return new EthBlockTest(file, defaultProfileId);
            });
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
    std::string defaultProfileId = "eth-cancun";
    for (int i = 2; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--fork-profile" && i + 1 < argc)
            defaultProfileId = argv[++i];
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
            fs::relative(f, fixturesDir).parent_path().string(), f, defaultProfileId);

    return RUN_ALL_TESTS();
}
