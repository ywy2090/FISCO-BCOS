#define BOOST_TEST_MODULE ManifestLoaderTest
#include "bcos-evm/evm-reference-tests/ManifestLoader.h"
#include <boost/test/included/unit_test.hpp>
#include <filesystem>
#include <fstream>

namespace bcos::evm::reference_tests
{
namespace
{
std::filesystem::path smokeManifestPath()
{
#ifdef EVM_REFERENCE_TESTS_MANIFEST_DIR
    return std::filesystem::path(EVM_REFERENCE_TESTS_MANIFEST_DIR) / "eth-gst-prague-smoke.json";
#else
    return std::filesystem::path("manifests/eth-gst-prague-smoke.json");
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
    BOOST_CHECK_EQUAL(entries.front().assertLevels.size(), 1u);
    BOOST_CHECK_EQUAL(entries.front().assertLevels.front(), "transitional");
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
