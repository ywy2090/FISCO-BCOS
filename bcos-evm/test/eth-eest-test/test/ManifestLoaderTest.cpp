#define BOOST_TEST_MODULE ManifestLoaderTest
#include "bcos-evm/eth-eest-test/ManifestLoader.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include <boost/test/included/unit_test.hpp>
#include <filesystem>
#include <fstream>

namespace bcos::evm::reference_tests
{
namespace
{
std::filesystem::path smokeManifestPath()
{
#ifdef SPECS_TESTS_MANIFEST_DIR
    return std::filesystem::path(SPECS_TESTS_MANIFEST_DIR) / "eth/eth-gst-prague-smoke.json";
#else
    return std::filesystem::path("manifests/eth/eth-gst-prague-smoke.json");
#endif
}
}  // namespace

BOOST_AUTO_TEST_CASE(loads_prague_smoke_manifest)
{
    auto const entries = loadManifest(smokeManifestPath());
    BOOST_REQUIRE_GE(entries.size(), 1u);
    BOOST_CHECK(entries.front().evidenceKind == EvidenceKind::ReferenceParity);
    BOOST_CHECK(entries.front().path == ExecutionPath::Reference);
    BOOST_CHECK_EQUAL(entries.front().forkProfileId, "eth-prague");
    BOOST_CHECK_EQUAL(entries.front().assertLevels.size(), 3u);
    BOOST_CHECK_EQUAL(entries.front().assertLevels[0], "expectException");
    BOOST_CHECK_EQUAL(entries.front().assertLevels[1], "stateRoot");
    BOOST_CHECK_EQUAL(entries.front().assertLevels[2], "logsHash");
}

BOOST_AUTO_TEST_CASE(resolve_execution_profile_applies_post_fork_policy)
{
    auto const osaka = ForkProfileRegistry::instance().findByProfileId("eth-osaka");
    BOOST_REQUIRE(osaka.has_value());
    auto const resolved =
        ForkProfileRegistry::instance().resolveExecutionProfile(*osaka, std::string{"Prague"});
    BOOST_CHECK_EQUAL(resolved.revision.revision, EVMC_OSAKA);
    BOOST_CHECK(!resolved.revision.eip7825);
    BOOST_CHECK(resolved.revision.eip7623);
    BOOST_CHECK(!resolved.revision.eip7823);
}

BOOST_AUTO_TEST_CASE(osaka_smoke_manifest_carries_prague_post_fork)
{
#ifdef SPECS_TESTS_MANIFEST_DIR
    auto const path =
        std::filesystem::path(SPECS_TESTS_MANIFEST_DIR) / "eth/eth-gst-osaka-smoke.json";
#else
    auto const path = std::filesystem::path("manifests/eth/eth-gst-osaka-smoke.json");
#endif
    auto const entries = loadManifest(path);
    BOOST_REQUIRE(!entries.empty());
    BOOST_REQUIRE(entries.front().postFork.has_value());
    BOOST_CHECK_EQUAL(*entries.front().postFork, "Prague");
}

BOOST_AUTO_TEST_CASE(rejects_missing_required_field)
{
    auto const tempPath = std::filesystem::temp_directory_path() / "invalid-manifest.json";
    {
        std::ofstream out(tempPath);
        out << R"({"manifestVersion":1,"entries":[{"evidenceId":"x"}]})";
    }

    BOOST_CHECK_THROW(loadManifest(tempPath), std::runtime_error);
    std::filesystem::remove(tempPath);
}

BOOST_AUTO_TEST_CASE(loads_optional_variant_key)
{
    auto const tempPath = std::filesystem::temp_directory_path() / "manifest-with-variant-key.json";
    {
        std::ofstream out(tempPath);
        out << R"({
  "manifestVersion": 1,
  "entries": [{
    "evidenceId": "eth.gst.prague.smoke.self_balance",
    "sourceSuite": "ethereum-tests",
    "casePath": "GeneralStateTests/stSelfBalance/selfBalance.json",
    "variantKey": "GeneralStateTests/stSelfBalance/selfBalance.json::selfBalance-fork_[Cancun-Prague]-d0g0v0",
    "forkProfileId": "eth-prague",
    "path": "Reference",
    "evidenceKind": "ReferenceParity",
    "capabilityRowIds": ["eip2929-runtime-warm"],
    "assertLevels": ["transitional"]
  }]
})";
    }

    auto const entries = loadManifest(tempPath);
    BOOST_REQUIRE_EQUAL(entries.size(), 1u);
    BOOST_REQUIRE(entries.front().variantKey.has_value());
    BOOST_CHECK_EQUAL(*entries.front().variantKey,
        "GeneralStateTests/stSelfBalance/"
        "selfBalance.json::selfBalance-fork_[Cancun-Prague]-d0g0v0");

    std::filesystem::remove(tempPath);
}

BOOST_AUTO_TEST_CASE(rejects_empty_variant_key)
{
    auto const tempPath =
        std::filesystem::temp_directory_path() / "manifest-empty-variant-key.json";
    {
        std::ofstream out(tempPath);
        out << R"({
  "manifestVersion": 1,
  "entries": [{
    "evidenceId": "x",
    "sourceSuite": "ethereum-tests",
    "casePath": "GeneralStateTests/stExample/add11.json",
    "variantKey": "",
    "forkProfileId": "eth-prague",
    "path": "Reference",
    "evidenceKind": "ReferenceParity",
    "capabilityRowIds": ["eip2929-runtime-warm"],
    "assertLevels": ["transitional"]
  }]
})";
    }

    BOOST_CHECK_THROW(loadManifest(tempPath), std::runtime_error);
    std::filesystem::remove(tempPath);
}

}  // namespace bcos::evm::reference_tests
