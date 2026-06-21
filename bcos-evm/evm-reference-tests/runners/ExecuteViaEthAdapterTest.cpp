#define BOOST_TEST_MODULE ExecuteViaEthAdapterTest
#include "bcos-evm/evm-reference-tests/ExecuteViaEthAdapter.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/evm-reference-tests/ForkProfileRegistry.h"
#include "bcos-evm/evm-reference-tests/GeneralStateTestLoader.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(runs_self_balance_via_execute_via_eth)
{
    auto const root = resolveEthereumTestsRoot();
    ensureGeneralStateTestsExtracted(root);

    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-prague");
    BOOST_REQUIRE(profile.has_value());

    auto const testCase = loadGeneralStateTest(
        root / "GeneralStateTests/stSelfBalance/selfBalance.json",
        std::string_view{
            "GeneralStateTests/stSelfBalance/selfBalance.json::selfBalance-fork_[Cancun-Prague]-"
            "d0g0v0"});

    auto const subtests = listSubtests(testCase, profile->upstreamForkName);
    BOOST_REQUIRE(!subtests.empty());

    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ExecuteViaEthAdapter adapter(*profile, hashImpl, vm);

    auto const result = bcos::task::syncWait(adapter.execute(testCase, subtests.front()));
    BOOST_CHECK(result.status != EVMC_INTERNAL_ERROR);
}

}  // namespace bcos::evm::reference_tests
