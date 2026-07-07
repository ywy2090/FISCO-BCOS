#define BOOST_TEST_MODULE BlockchainTestLoaderTest
#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <filesystem>

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

}  // namespace bcos::evm::reference_tests
