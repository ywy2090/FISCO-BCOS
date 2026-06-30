#define BOOST_TEST_MODULE ExecuteViaHostImportedFixtureTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "fixtures/FiscoFixtureAdapter.h"
#include "fixtures/HostFixtureAssert.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using namespace fixtures;

BOOST_AUTO_TEST_CASE(imported_fixture_plain_call_via_execute_via_host)
{
    // stEIP7702_delegation.json: unsigned auth tuples are signed at load time; delegated CALL to
    // 0xbb returning 42.
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    auto const path =
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR) / "imported" / "stEIP7702_delegation.json"
#else
        std::filesystem::path("fixtures/state/imported/stEIP7702_delegation.json")
#endif
        ;
    auto fixture = loadFixture(path);
    state::test::InMemoryStateView view;
    for (auto const& [addr, acct] : fixture.preState)
        view.insert_account(addr, acct);
    auto input = buildFiscoExecutionRequest(fixture, view, vm, hashImpl);
    int64_t const gasBefore = input.message.gas;
    auto output = task::syncWait(applyFiscoMessage(std::move(input)));
    assertHostFixtureResult(fixture, output, gasBefore);
}

}  // namespace bcos::evm::test
