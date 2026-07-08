#define BOOST_TEST_MODULE BlockchainTestLoaderTest
#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include "bcos-evm/eth-eest-test/BlockchainPostStateAssert.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace bcos::evm::reference_tests
{
namespace
{
std::filesystem::path fixture()
{
    return std::filesystem::path(SPECS_TESTS_EEST_ROOT) /
           "fixtures/blockchain_tests/cancun/eip5656_mcopy/test_mcopy_on_empty_memory.json";
}
}  // namespace

BOOST_AUTO_TEST_CASE(loads_cancun_valid_block_fixture)
{
    boost::property_tree::ptree root;
    boost::property_tree::read_json(fixture().string(), root);

    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    auto const& t = tests.front();
    BOOST_CHECK_EQUAL(t.network, "Cancun");
    BOOST_CHECK_EQUAL(t.genesisBlockHeader.blockNumber, 0);
    BOOST_REQUIRE(!t.testBlocks.empty());
    BOOST_CHECK(t.testBlocks.front().expectException.empty());
    BOOST_CHECK(std::any_of(std::begin(t.lastBlockHash.bytes), std::end(t.lastBlockHash.bytes),
        [](uint8_t b) { return b != 0; }));
}

BOOST_AUTO_TEST_CASE(loads_invalid_block_via_rlp_decoded)
{
    boost::property_tree::ptree root;
    auto p = std::filesystem::path(SPECS_TESTS_EEST_ROOT) /
             "fixtures/blockchain_tests/cancun/eip4844_blobs/"
             "test_invalid_blob_gas_used_in_header.json";
    boost::property_tree::read_json(p.string(), root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    bool sawInvalid = false;
    for (auto const& t : tests)
    {
        for (auto const& b : t.testBlocks)
        {
            if (!b.expectException.empty())
                sawInvalid = true;
        }
    }
    BOOST_CHECK(sawInvalid);
}

BOOST_AUTO_TEST_CASE(loads_blob_schedule_and_chain_id)
{
    boost::property_tree::ptree root;
    boost::property_tree::read_json(fixture().string(), root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    BOOST_CHECK(tests.front().chainId > 0);
    BOOST_CHECK(tests.front().blobSchedule.count("Cancun") > 0);
}

BOOST_AUTO_TEST_CASE(captures_raw_tx_rlp_for_cancun_block)
{
    boost::property_tree::ptree root;
    boost::property_tree::read_json(fixture().string(), root);
    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE(!tests.empty());
    auto const& blk = tests.front().testBlocks.front();
    BOOST_CHECK_EQUAL(blk.rawTxRlp.size(), blk.transactions.size());
    if (!blk.rawTxRlp.empty())
        BOOST_CHECK(!blk.rawTxRlp.front().empty());
}

BOOST_AUTO_TEST_CASE(post_state_null_account_maps_to_absent)
{
    // EEST partial semantics: a `null` postState account means "must be absent",
    // distinct from an empty object `{}` (presence-only, "must exist"). boost's
    // json_parser preserves the null literal (data()=="null"), so the loader can
    // tell them apart.
    std::string const js = R"({
      "null_absent_case": {
        "network": "Cancun",
        "genesisBlockHeader": {"number": "0x0"},
        "pre": {},
        "postState": {
          "0x1111111111111111111111111111111111111111": null,
          "0x2222222222222222222222222222222222222222": {},
          "0x3333333333333333333333333333333333333333": {"nonce": "0x1"}
        }
      }
    })";
    boost::property_tree::ptree root;
    std::istringstream in(js);
    boost::property_tree::read_json(in, root);

    auto tests = loadBlockchainTests(root);
    BOOST_REQUIRE_EQUAL(tests.size(), 1U);
    auto const& accounts = tests.front().postExpectation.accounts;
    BOOST_REQUIRE_EQUAL(accounts.size(), 3U);

    // Order preserved: null → Absent; {} → presence-only Present; {nonce} → Present+hasNonce.
    BOOST_CHECK(accounts[0].second.kind == ExpectedPostAccount::Kind::Absent);

    BOOST_CHECK(accounts[1].second.kind == ExpectedPostAccount::Kind::Present);
    BOOST_CHECK(!accounts[1].second.hasNonce);
    BOOST_CHECK(!accounts[1].second.hasBalance);
    BOOST_CHECK(!accounts[1].second.hasCode);
    BOOST_CHECK(!accounts[1].second.hasStorage);

    BOOST_CHECK(accounts[2].second.kind == ExpectedPostAccount::Kind::Present);
    BOOST_CHECK(accounts[2].second.hasNonce);
    BOOST_CHECK_EQUAL(accounts[2].second.nonce, 1U);

    // Absent accounts are not mirrored into the raw legacy postState vector.
    auto const& raw = tests.front().postState;
    BOOST_CHECK_EQUAL(raw.size(), 2U);
}

}  // namespace bcos::evm::reference_tests
