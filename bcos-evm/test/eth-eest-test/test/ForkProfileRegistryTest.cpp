#define BOOST_TEST_MODULE ForkProfileRegistryTest
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(prague_profile_maps_upstream_fork)
{
    auto const profile = ForkProfileRegistry::instance().findByUpstreamFork("Prague");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->profileId, "eth-prague");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_PRAGUE);
    BOOST_CHECK(profile->revision.eip7702);
}

BOOST_AUTO_TEST_CASE(cancun_profile_maps_by_id)
{
    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-cancun");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->upstreamForkName, "Cancun");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_CANCUN);
    BOOST_CHECK(!profile->revision.eip7702);
}

BOOST_AUTO_TEST_CASE(reference_path_is_only_supported_profile)
{
    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-prague");
    BOOST_REQUIRE(profile.has_value());
    BOOST_REQUIRE_EQUAL(profile->pathProfiles.size(), 1u);
    BOOST_CHECK(profile->pathProfiles.front().path == ExecutionPath::Reference);
    BOOST_CHECK(profile->pathProfiles.front().evidenceKind == EvidenceKind::ReferenceParity);
    BOOST_CHECK(!profile->pathProfiles.front().unsupportedReason.has_value());
}

BOOST_AUTO_TEST_CASE(osaka_profile_maps_upstream_fork)
{
    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-osaka");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->upstreamForkName, "Osaka");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_OSAKA);
    BOOST_CHECK(profile->revision.eip7212);
    BOOST_CHECK(profile->revision.eip7823);
    BOOST_CHECK(profile->revision.eip7702);
    BOOST_REQUIRE_EQUAL(profile->pathProfiles.size(), 1u);
    BOOST_CHECK(profile->pathProfiles.front().evidenceKind == EvidenceKind::ReferenceParity);
}

BOOST_AUTO_TEST_CASE(osaka_profile_find_by_upstream_fork)
{
    auto const profile = ForkProfileRegistry::instance().findByUpstreamFork("Osaka");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->profileId, "eth-osaka");
}

BOOST_AUTO_TEST_CASE(unknown_profile_returns_empty)
{
    BOOST_CHECK(!ForkProfileRegistry::instance().findByProfileId("eth-unknown").has_value());
    BOOST_CHECK(!ForkProfileRegistry::instance().findByUpstreamFork("Frontier").has_value());
}

}  // namespace bcos::evm::reference_tests
