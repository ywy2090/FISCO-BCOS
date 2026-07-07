#define BOOST_TEST_MODULE EestStateFullManifestTest
#include "bcos-evm/eth-eest-test/EestStateFullManifest.h"
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <set>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(default_profiles_match_manifest_unique_ids)
{
    auto const& index = StateFullManifestIndex::instance();
    auto const defaults = index.defaultGranularProfileIds();
    BOOST_REQUIRE(!defaults.empty());

    std::set<std::string_view> seen;
    for (auto const id : defaults)
    {
        BOOST_CHECK(seen.insert(id).second);
    }

    BOOST_CHECK(std::find(defaults.begin(), defaults.end(), "eth-shanghai") != defaults.end());
    BOOST_CHECK(std::find(defaults.begin(), defaults.end(), "eth-cancun") != defaults.end());
    BOOST_CHECK(std::find(defaults.begin(), defaults.end(), "eth-prague") != defaults.end());
    BOOST_CHECK(std::find(defaults.begin(), defaults.end(), "eth-osaka") != defaults.end());
}

BOOST_AUTO_TEST_CASE(manifest_dir_lookup_prague7623_uses_osaka)
{
    auto const id = StateFullManifestIndex::instance().profileIdForRelativeDir(
        "prague/eip7623_increase_calldata_cost");
    BOOST_REQUIRE(id.has_value());
    BOOST_CHECK_EQUAL(*id, "eth-osaka");
}

BOOST_AUTO_TEST_CASE(manifest_dir_lookup_cancun4844_uses_cancun)
{
    auto const id =
        StateFullManifestIndex::instance().profileIdForRelativeDir("cancun/eip4844_blobs");
    BOOST_REQUIRE(id.has_value());
    BOOST_CHECK_EQUAL(*id, "eth-cancun");
}

BOOST_AUTO_TEST_CASE(distinct_manifest_profile_detects_prague_osaka_dirs)
{
    auto const& index = StateFullManifestIndex::instance();
    BOOST_CHECK(
        index.isDistinctManifestProfile("prague/eip7623_increase_calldata_cost", "eth-osaka"));
    BOOST_CHECK(
        !index.isDistinctManifestProfile("prague/eip2537_bls_12_381_precompiles", "eth-prague"));
}

}  // namespace bcos::evm::reference_tests
