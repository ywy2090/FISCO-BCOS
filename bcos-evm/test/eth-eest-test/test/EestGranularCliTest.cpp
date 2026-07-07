#define BOOST_TEST_MODULE EestGranularCliTest
#include "bcos-evm/eth-eest-test/EestGranularCli.h"
#include <boost/test/included/unit_test.hpp>
#include <stdexcept>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(parse_multi_path_and_k_filter)
{
    const char* argv[] = {
        "prog", "path/a", "path/b", "-k", "4844", "--fork-profiles", "eth-cancun"};
    int argc = 7;
    auto parsed = parseEestGranularCliRemaining(argc, const_cast<char**>(argv));
    BOOST_REQUIRE_EQUAL(parsed.paths.size(), 2u);
    BOOST_REQUIRE(parsed.nameFilter.has_value());
    BOOST_CHECK_EQUAL(*parsed.nameFilter, "4844");
    BOOST_REQUIRE_EQUAL(parsed.profileIds.size(), 1u);
    BOOST_CHECK_EQUAL(parsed.profileIds[0], "eth-cancun");
}

BOOST_AUTO_TEST_CASE(build_runner_config_rejects_unknown_profile)
{
    BOOST_CHECK_THROW(buildRunnerConfig({"eth-not-a-fork"}), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(build_runner_config_defaults_from_manifest)
{
    auto const profiles = buildRunnerConfig({});
    BOOST_REQUIRE_GE(profiles.size(), 4u);
    auto hasId = [&](char const* id) {
        return std::ranges::any_of(
            profiles, [&](ForkProfile const& p) { return p.profileId == id; });
    };
    BOOST_CHECK(hasId("eth-shanghai"));
    BOOST_CHECK(hasId("eth-cancun"));
    BOOST_CHECK(hasId("eth-prague"));
    BOOST_CHECK(hasId("eth-osaka"));
    BOOST_CHECK(hasId("eth-homestead"));
    BOOST_CHECK(hasId("eth-berlin"));
    BOOST_CHECK(hasId("eth-london"));
    BOOST_CHECK(hasId("eth-paris"));
}

}  // namespace bcos::evm::reference_tests
