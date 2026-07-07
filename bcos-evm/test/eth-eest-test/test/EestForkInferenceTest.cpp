#define BOOST_TEST_MODULE EestForkInferenceTest
#include "bcos-evm/eth-eest-test/EestForkInference.h"
#include "bcos-evm/eth-eest-test/EestGranularCli.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include <boost/test/included/unit_test.hpp>
#include <filesystem>
#include <ranges>

namespace fs = std::filesystem;

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(manifest_map_prague7623_uses_osaka)
{
    fs::path root = "/fixtures/state_tests";
    auto const id =
        manifestProfileIdForPath(root / "prague/eip7623_increase_calldata_cost/x.json", root);
    BOOST_REQUIRE(id.has_value());
    BOOST_CHECK_EQUAL(*id, "eth-osaka");
}

BOOST_AUTO_TEST_CASE(manifest_map_prague7702_uses_osaka)
{
    fs::path root = "/fixtures/state_tests";
    auto const id = manifestProfileIdForPath(root / "prague/eip7702_set_code_tx/x.json", root);
    BOOST_REQUIRE(id.has_value());
    BOOST_CHECK_EQUAL(*id, "eth-osaka");
}

BOOST_AUTO_TEST_CASE(manifest_map_cancun_uses_cancun)
{
    fs::path root = "/fixtures/state_tests";
    auto const id = manifestProfileIdForPath(root / "cancun/eip4844_blobs/x.json", root);
    BOOST_REQUIRE(id.has_value());
    BOOST_CHECK_EQUAL(*id, "eth-cancun");
}

BOOST_AUTO_TEST_CASE(infer_upstream_fork_from_path)
{
    fs::path root = "/fixtures/state_tests";
    auto const fork = inferUpstreamForkFromPath(root / "cancun/eip4844_blobs/x.json", root);
    BOOST_REQUIRE(fork.has_value());
    BOOST_CHECK_EQUAL(*fork, "Cancun");
}

BOOST_AUTO_TEST_CASE(resolve_runs_uses_post_fork_key)
{
    StateTestCase tc;
    tc.name = "fixture";
    tc.postByFork.emplace("Osaka", std::vector<ExpectedPostState>{});

    fs::path root = "/fixtures/state_tests";
    fs::path file = root / "prague/eip7623_increase_calldata_cost/x.json";

    auto const osaka = ForkProfileRegistry::instance().findByProfileId("eth-osaka");
    BOOST_REQUIRE(osaka.has_value());
    std::vector<ForkProfile> filter = {*osaka};

    auto const runs = resolveRunsForCase(tc, file, root, filter);
    BOOST_REQUIRE_EQUAL(runs.size(), 1u);
    BOOST_CHECK_EQUAL(runs.front().postForkKey, "Osaka");
    BOOST_CHECK_EQUAL(runs.front().executionProfile.profileId, "eth-osaka");
}

BOOST_AUTO_TEST_CASE(resolve_runs_skips_non_matching_fork_with_single_profile_filter)
{
    StateTestCase tc;
    tc.name = "shanghai_only";
    tc.postByFork.emplace("Shanghai", std::vector<ExpectedPostState>{});

    fs::path root = "/fixtures/state_tests";
    fs::path file = root / "cancun/eip4844_blobs/x.json";

    auto const cancun = ForkProfileRegistry::instance().findByProfileId("eth-cancun");
    BOOST_REQUIRE(cancun.has_value());
    std::vector<ForkProfile> filter = {*cancun};

    auto const runs = resolveRunsForCase(tc, file, root, filter);
    BOOST_CHECK(runs.empty());
}

BOOST_AUTO_TEST_CASE(resolve_runs_homestead_post_on_homestead_fixture_path)
{
    StateTestCase tc;
    tc.name = "homestead_fixture";
    tc.postByFork.emplace("Homestead", std::vector<ExpectedPostState>{});

    fs::path root = "/fixtures/state_tests";
    fs::path file = root / "frontier/opcodes/test_value_transfer_gas_calculation_homestead.json";

    auto const profiles = buildRunnerConfig({});
    auto const runs = resolveRunsForCase(tc, file, root, profiles);
    BOOST_REQUIRE(!runs.empty());
    BOOST_CHECK_EQUAL(runs.front().postForkKey, "Homestead");
    BOOST_CHECK_EQUAL(runs.front().executionProfile.profileId, "eth-homestead");
}

BOOST_AUTO_TEST_CASE(resolve_runs_skips_berlin_post_on_unlisted_frontier_opcode)
{
    StateTestCase tc;
    tc.name = "all_opcodes";
    tc.postByFork.emplace("Berlin", std::vector<ExpectedPostState>{});

    fs::path root = "/fixtures/state_tests";
    fs::path file = root / "frontier/opcodes/test_all_opcodes.json";

    auto const profiles = buildRunnerConfig({});
    auto const runs = resolveRunsForCase(tc, file, root, profiles);
    BOOST_CHECK(runs.empty());
}

BOOST_AUTO_TEST_CASE(resolve_runs_london_post_on_london_fixture_path)
{
    StateTestCase tc;
    tc.name = "london_fixture";
    tc.postByFork.emplace("London", std::vector<ExpectedPostState>{});

    fs::path root = "/fixtures/state_tests";
    fs::path file = root / "london/eip1559_fee_market_change/test_eip1559_tx_validity.json";

    auto const profiles = buildRunnerConfig({});
    auto const runs = resolveRunsForCase(tc, file, root, profiles);
    BOOST_REQUIRE(!runs.empty());
    BOOST_CHECK_EQUAL(runs.front().postForkKey, "London");
    BOOST_CHECK_EQUAL(runs.front().executionProfile.profileId, "eth-london");
}

BOOST_AUTO_TEST_CASE(resolve_runs_paris_post_on_paris_fixture_path)
{
    StateTestCase tc;
    tc.name = "paris_fixture";
    tc.postByFork.emplace("Paris", std::vector<ExpectedPostState>{});

    fs::path root = "/fixtures/state_tests";
    fs::path file = root / "paris/eip7610_create_collision/test_schema.json";

    auto const profiles = buildRunnerConfig({});
    auto const runs = resolveRunsForCase(tc, file, root, profiles);
    BOOST_REQUIRE(!runs.empty());
    BOOST_CHECK_EQUAL(runs.front().postForkKey, "Paris");
    BOOST_CHECK_EQUAL(runs.front().executionProfile.profileId, "eth-paris");
}

}  // namespace bcos::evm::reference_tests
