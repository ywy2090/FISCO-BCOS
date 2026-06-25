#define BOOST_TEST_MODULE ExecuteViaEthFixtureTest
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/reference/EthReferenceBridge.h"
#include "fixtures/EthFixtureAdapter.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "fixtures/FixtureAssert.h"
#include "helpers/ApplyStateDiffToView.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using namespace fixtures;

BOOST_AUTO_TEST_CASE(existing_prague_fixtures_via_execute_via_eth)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto const files = listAllFixtureFiles(
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR)
#else
        std::filesystem::path("fixtures/state")
#endif
    );
    BOOST_REQUIRE_GE(files.size(), 5u);
    for (auto const& path : files)
    {
        auto fixture = loadFixture(path);
        BOOST_TEST_CONTEXT("fixture=" << fixture.name << " path=" << path.string())
        {
            state::test::InMemoryEvmStateReader view;
            for (auto const& [addr, acct] : fixture.preState)
                view.insert_account(addr, acct);
            auto input = buildEthReferenceRequest(fixture, view, vm, hashImpl);
            int64_t const gasBefore = input.message.gas;
            auto output = task::syncWait(ethReferenceExecute(std::move(input)));
            assertFixtureResult(fixture, output, gasBefore);
            if (!fixture.expected.post.empty())
            {
                applyStateDiffToView(output.stateDiff, view);
                assertFixturePostState(view, fixture);
            }
        }
    }
}
}  // namespace bcos::evm::test
