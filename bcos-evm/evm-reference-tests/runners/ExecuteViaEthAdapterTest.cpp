#define BOOST_TEST_MODULE ExecuteViaEthAdapterTest
#include "bcos-evm/evm-reference-tests/ExecuteViaEthAdapter.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/evm-reference-tests/ForkProfileRegistry.h"
#include "bcos-evm/evm-reference-tests/GeneralStateTestLoader.h"
#include "bcos-evm/evm-reference-tests/GstStateHash.h"
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

BOOST_AUTO_TEST_CASE(add11_state_root_matches_fixture)
{
    auto const root = resolveEthereumTestsRoot();
    ensureGeneralStateTestsExtracted(root);

    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-prague");
    BOOST_REQUIRE(profile.has_value());

    auto const testCase = loadGeneralStateTest(root / "GeneralStateTests/stExample/add11.json",
        std::string_view{
            "GeneralStateTests/stExample/add11.json::add11-fork_[Cancun-Prague]-d0g0v0"});

    auto const subtests = listSubtests(testCase, profile->upstreamForkName);
    BOOST_REQUIRE(!subtests.empty());

    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    ExecuteViaEthAdapter adapter(*profile, hashImpl, vm);

    auto const result = bcos::task::syncWait(adapter.execute(testCase, subtests.front()));
    BOOST_REQUIRE_EQUAL(result.status, EVMC_SUCCESS);

    evmc_bytes32 const expected = {0xe8, 0x01, 0x0c, 0xe5, 0x90, 0xf4, 0x01, 0xc9, 0xd6, 0x1f, 0xef,
        0x8a, 0xb0, 0x5b, 0xea, 0x9b, 0xce, 0xc2, 0x42, 0x81, 0xb7, 0x95, 0xe5, 0x86, 0x88, 0x09,
        0xbc, 0x4e, 0x51, 0x5a, 0xa5, 0x30};
    BOOST_REQUIRE(result.stateRoot.has_value());
    BOOST_CHECK_EQUAL(std::memcmp(result.stateRoot->bytes, expected.bytes, 32), 0);
    BOOST_CHECK_EQUAL(result.gasUsed, 43112);

    auto const postState =
        buildPostStateView(testCase.preState, result.stateDiff, true, testCase.env.coinbase, true);

    auto findAccount = [&](std::string_view hexAddr) -> state::Account const* {
        auto const addr = state::parseHexAddress(hexAddr);
        for (auto const& [address, account] : postState.accounts)
        {
            if (state::AddressEqual{}(address, addr))
            {
                return &account;
            }
        }
        return nullptr;
    };

    auto const* contract = findAccount("0x095e7baea6a6c7c4c2dfeb977efac326af552d87");
    auto const* sender = findAccount("0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b");
    auto const* coinbase = findAccount("0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba");
    BOOST_REQUIRE(contract != nullptr);
    BOOST_REQUIRE(sender != nullptr);
    BOOST_REQUIRE(coinbase != nullptr);

    BOOST_CHECK_EQUAL(contract->balance, bcos::fromBigQuantity("0x0de0b6b3a76586a0"));
    BOOST_CHECK_EQUAL(sender->balance, bcos::fromBigQuantity("0x0de0b6b3a75be550"));
    BOOST_CHECK_EQUAL(sender->nonce, 1u);
    BOOST_CHECK_EQUAL(coinbase->nonce, 1u);
    BOOST_CHECK_EQUAL(coinbase->balance, 0u);

    evmc_bytes32 slot{};
    evmc_bytes32 expectedValue{};
    expectedValue.bytes[31] = 0x02;
    auto const storageIt = contract->storage.find(slot);
    BOOST_REQUIRE(storageIt != contract->storage.end());
    BOOST_CHECK(state::Bytes32Equal{}(storageIt->second, expectedValue));
}

}  // namespace bcos::evm::reference_tests
