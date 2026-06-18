#define BOOST_TEST_MODULE PragueStateTest
#include "bcos-evm/eth/execution/BlockInfoBuilder.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/eth/state/transition.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::state::test
{
namespace
{
namespace pt = boost::property_tree;

struct ExpectedResult
{
    evmc_status_code status = EVMC_SUCCESS;
    int64_t gasUsed = 0;
    bcos::bytes output;
    size_t logs = 0;
};

struct FixtureCase
{
    std::string name;
    Transaction tx;
    BlockInfo block;
    TransactionProperties txProps;
    std::vector<std::pair<evmc_address, Account>> preState;
    ExpectedResult expected;
};

evmc_status_code parseStatus(std::string_view status)
{
    if (status == "EVMC_SUCCESS" || status == "SUCCESS")
    {
        return EVMC_SUCCESS;
    }
    if (status == "EVMC_REVERT" || status == "REVERT")
    {
        return EVMC_REVERT;
    }
    if (status == "EVMC_OUT_OF_GAS" || status == "OUT_OF_GAS")
    {
        return EVMC_OUT_OF_GAS;
    }
    BOOST_FAIL("Unsupported status in fixture: " << status);
    return EVMC_INTERNAL_ERROR;
}

evmc_address parseAddress(const std::string& value)
{
    auto const address = parseHexAddress(value);
    BOOST_REQUIRE_MESSAGE(
        !(isZeroAddress(address) && value != "0x0000000000000000000000000000000000000000"),
        "Invalid address literal: " << value);
    return address;
}

bcos::u256 parseU256(std::string_view value)
{
    if (value.empty())
    {
        return 0;
    }
    return bcos::fromBigQuantity(value);
}

bcos::bytes parseBytes(const std::string& value)
{
    return bcos::fromHex(value);
}

BlockInfo parseBlock(const pt::ptree& tree)
{
    auto builder = execution::BlockInfoBuilder()
                       .number(tree.get<int64_t>("number", 0))
                       .timestamp(tree.get<int64_t>("timestamp", 0))
                       .gasLimit(tree.get<int64_t>("gas_limit", 0))
                       .coinbase(parseAddress(tree.get<std::string>(
                           "coinbase", "0x0000000000000000000000000000000000000000")))
                       .baseFee(parseU256(tree.get<std::string>("base_fee", "0x0")))
                       .chainId(parseU256(tree.get<std::string>("chain_id", "0x1")))
                       .blobBaseFee(parseU256(tree.get<std::string>("blob_base_fee", "0x0")));
    return builder.build();
}

FixtureCase loadFixture(const std::filesystem::path& path)
{
    std::ifstream file(path);
    BOOST_REQUIRE_MESSAGE(file.good(), "Failed to open fixture: " << path.string());

    pt::ptree tree;
    pt::read_json(file, tree);

    FixtureCase fixture;
    fixture.name = tree.get<std::string>("name");
    auto const rev = tree.get<std::string>("revision", "prague");
    BOOST_REQUIRE_MESSAGE(rev == "prague" || rev == "Prague",
        "Only Prague vectors are enabled in C2-2, got: " << rev);

    auto const& txTree = tree.get_child("tx");
    fixture.tx.from = parseAddress(txTree.get<std::string>("from"));
    if (auto to = txTree.get_optional<std::string>("to"); to.has_value())
    {
        fixture.tx.to = parseAddress(*to);
    }
    fixture.tx.gasLimit = txTree.get<int64_t>("gas_limit");
    fixture.tx.gasPrice = parseU256(txTree.get<std::string>("gas_price", "0x0"));
    fixture.tx.value = parseU256(txTree.get<std::string>("value", "0x0"));
    fixture.tx.nonce = txTree.get<uint64_t>("nonce", 0);
    fixture.tx.data = parseBytes(txTree.get<std::string>("data", "0x"));

    fixture.txProps.warmCoinbase = tree.get<bool>("tx_props.warm_coinbase", true);
    fixture.txProps.warmDestination = tree.get<bool>("tx_props.warm_destination", true);
    fixture.txProps.isStatic = tree.get<bool>("tx_props.is_static", false);

    fixture.block = parseBlock(tree.get_child("block"));

    if (auto pre = tree.get_child_optional("pre"); pre.has_value())
    {
        for (auto const& accountNode : *pre)
        {
            auto const& accountTree = accountNode.second;
            auto const address = parseAddress(accountTree.get<std::string>("address"));
            Account account;
            account.balance = parseU256(accountTree.get<std::string>("balance", "0x0"));
            account.nonce = accountTree.get<uint64_t>("nonce", 0);
            account.code = parseBytes(accountTree.get<std::string>("code", "0x"));
            fixture.preState.emplace_back(address, std::move(account));
        }
    }

    auto const& expectedTree = tree.get_child("expected");
    fixture.expected.status = parseStatus(expectedTree.get<std::string>("status", "EVMC_SUCCESS"));
    fixture.expected.gasUsed = expectedTree.get<int64_t>("gas_used", 0);
    fixture.expected.output = parseBytes(expectedTree.get<std::string>("output", "0x"));
    fixture.expected.logs = expectedTree.get<size_t>("logs", 0);

    return fixture;
}

bool sameBytes(const bcos::bytes& lhs, const bcos::bytes& rhs)
{
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

std::vector<std::filesystem::path> fixtureFiles()
{
    std::filesystem::path fixtureDir =
#ifdef PRAGUE_STATE_FIXTURES_DIR
        std::filesystem::path(PRAGUE_STATE_FIXTURES_DIR);
#else
        std::filesystem::path("fixtures/state");
#endif

    return {
        fixtureDir / "prague_call_empty_account.json",
        fixtureDir / "prague_call_return_word.json",
        fixtureDir / "prague_call_revert.json",
        fixtureDir / "prague_create_empty_initcode.json",
        fixtureDir / "prague_selfdestruct.json",
    };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PragueStateTest)

BOOST_AUTO_TEST_CASE(prague_minimal_vectors_gate)
{
    auto const files = fixtureFiles();
    for (auto const& path : files)
    {
        auto const fixture = loadFixture(path);

        BOOST_TEST_CONTEXT("fixture=" << fixture.name)
        {
            InMemoryStateView view;
            for (auto const& [address, account] : fixture.preState)
            {
                view.insert_account(address, account);
            }

            BlockHashes blockHashes = [](int64_t) { return evmc_bytes32{}; };
            evmc::VM vm{evmc_create_evmone()};

            auto const receipt = transition(view, fixture.block, blockHashes, fixture.tx,
                EVMC_PRAGUE, vm, fixture.txProps, nullptr);

            BOOST_CHECK_EQUAL(
                static_cast<int>(receipt.status), static_cast<int>(fixture.expected.status));
            BOOST_CHECK_EQUAL(receipt.gasUsed, fixture.expected.gasUsed);
            BOOST_CHECK_EQUAL(receipt.logs.size(), fixture.expected.logs);
            BOOST_CHECK_MESSAGE(sameBytes(receipt.output, fixture.expected.output),
                "output mismatch, actual=0x" << bcos::toHex(receipt.output) << " expected=0x"
                                             << bcos::toHex(fixture.expected.output));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::state::test
