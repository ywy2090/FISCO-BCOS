#define BOOST_TEST_MODULE PragueStateTest
#include "bcos-evm/eth/state/transition.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <filesystem>
#include <string>

namespace bcos::evm::state::test
{
namespace
{
using bcos::evm::test::fixtures::listRootFixtureFiles;
using bcos::evm::test::fixtures::loadFixture;
using bcos::evm::test::fixtures::sameBytes;

std::filesystem::path fixtureDir()
{
#ifdef PRAGUE_STATE_FIXTURES_DIR
    return std::filesystem::path(PRAGUE_STATE_FIXTURES_DIR);
#else
    return std::filesystem::path("fixtures/state");
#endif
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PragueStateTest)

BOOST_AUTO_TEST_CASE(prague_minimal_vectors_gate)
{
    auto const files = listRootFixtureFiles(fixtureDir());
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
