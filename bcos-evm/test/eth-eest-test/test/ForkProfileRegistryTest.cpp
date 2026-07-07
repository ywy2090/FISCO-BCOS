#define BOOST_TEST_MODULE ForkProfileRegistryTest
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include <boost/test/included/unit_test.hpp>
#include <algorithm>

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
    BOOST_CHECK(profile->revision.eip7825);
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

BOOST_AUTO_TEST_CASE(homestead_profile_maps_upstream_fork)
{
    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-homestead");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->upstreamForkName, "Homestead");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_HOMESTEAD);
}

BOOST_AUTO_TEST_CASE(find_berlin_london_paris_by_upstream_fork)
{
    auto& reg = ForkProfileRegistry::instance();
    BOOST_REQUIRE(reg.findByUpstreamFork("Berlin").has_value());
    BOOST_REQUIRE(reg.findByUpstreamFork("London").has_value());
    BOOST_REQUIRE(reg.findByUpstreamFork("Paris").has_value());
    BOOST_REQUIRE(reg.findByUpstreamFork("Merge").has_value());
    BOOST_CHECK(reg.findByUpstreamFork("Paris")->revision.revision == EVMC_PARIS);
}

BOOST_AUTO_TEST_CASE(frontier_profile_maps_upstream_fork)
{
    auto const profile = ForkProfileRegistry::instance().findByUpstreamFork("Frontier");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->profileId, "eth-frontier");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_FRONTIER);
}

BOOST_AUTO_TEST_CASE(byzantium_profile_maps_upstream_fork)
{
    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-byzantium");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->upstreamForkName, "Byzantium");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_BYZANTIUM);
}

BOOST_AUTO_TEST_CASE(constantinople_profile_maps_constantinoplefix)
{
    auto const profile = ForkProfileRegistry::instance().findByUpstreamFork("ConstantinopleFix");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->profileId, "eth-constantinople");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_PETERSBURG);
}

BOOST_AUTO_TEST_CASE(istanbul_profile_maps_upstream_fork)
{
    auto const profile = ForkProfileRegistry::instance().findByProfileId("eth-istanbul");
    BOOST_REQUIRE(profile.has_value());
    BOOST_CHECK_EQUAL(profile->upstreamForkName, "Istanbul");
    BOOST_CHECK_EQUAL(profile->revision.revision, EVMC_ISTANBUL);
}

BOOST_AUTO_TEST_CASE(unknown_profile_returns_empty)
{
    BOOST_CHECK(!ForkProfileRegistry::instance().findByProfileId("eth-unknown").has_value());
}

BOOST_AUTO_TEST_CASE(dir_segment_maps_to_profile_id)
{
    auto const& reg = ForkProfileRegistry::instance();
    auto const homestead = reg.profileIdForDirSegment("homestead");
    BOOST_REQUIRE(homestead.has_value());
    BOOST_CHECK_EQUAL(*homestead, "eth-homestead");
    auto const berlin = reg.profileIdForDirSegment("berlin");
    BOOST_REQUIRE(berlin.has_value());
    BOOST_CHECK_EQUAL(*berlin, "eth-berlin");
    auto const cancun = reg.profileIdForDirSegment("cancun");
    BOOST_REQUIRE(cancun.has_value());
    BOOST_CHECK_EQUAL(*cancun, "eth-cancun");
    auto const frontier = reg.profileIdForDirSegment("frontier");
    BOOST_REQUIRE(frontier.has_value());
    BOOST_CHECK_EQUAL(*frontier, "eth-frontier");
    auto const istanbul = reg.profileIdForDirSegment("istanbul");
    BOOST_REQUIRE(istanbul.has_value());
    BOOST_CHECK_EQUAL(*istanbul, "eth-istanbul");
}

BOOST_AUTO_TEST_CASE(all_profile_ids_lists_registry)
{
    auto const ids = ForkProfileRegistry::instance().allProfileIds();
    BOOST_REQUIRE_EQUAL(ids.size(), 12u);
    BOOST_CHECK(std::find(ids.begin(), ids.end(), "eth-osaka") != ids.end());
}

BOOST_AUTO_TEST_CASE(resolve_revision_maps_network_to_evmc_revision)
{
    auto const& reg = ForkProfileRegistry::instance();
    auto const cancun = reg.resolveRevision("Cancun", /*timestamp*/ 0);
    BOOST_REQUIRE(cancun.has_value());
    BOOST_CHECK_EQUAL(static_cast<int>(*cancun), static_cast<int>(EVMC_CANCUN));

    auto const unknown = reg.resolveRevision("NoSuchFork", 0);
    BOOST_CHECK(!unknown.has_value());
}

}  // namespace bcos::evm::reference_tests
