#define BOOST_TEST_MODULE GeneralStateTestLoaderTest
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include <boost/test/included/unit_test.hpp>
#include <filesystem>
#include <fstream>

namespace bcos::evm::reference_tests
{
namespace
{
std::filesystem::path sampleGstPath()
{
    auto const root = resolveEthereumTestsRoot();
    ensureGeneralStateTestsExtracted(root);
    return root / "GeneralStateTests/stSelfBalance/selfBalance.json";
}

std::filesystem::path writeMultiVariantFixture()
{
    auto const path = std::filesystem::temp_directory_path() / "specs-tests-multi-variant.json";
    std::ofstream out(path);
    out << R"({
  "suite/case.json::variant-a": {
    "env": {
      "currentCoinbase": "0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba",
      "currentGasLimit": "0x0f4240",
      "currentNumber": "0x01",
      "currentTimestamp": "0x03e8"
    },
    "pre": {
      "0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b": {
        "balance": "0x0186a0",
        "code": "0x",
        "nonce": "0x00",
        "storage": {}
      }
    },
    "transaction": {
      "data": ["0x"],
      "gasLimit": ["0x5208"],
      "gasPrice": "0x0a",
      "nonce": "0x00",
      "secretKey": "0x00",
      "to": "0x1000000000000000000000000000000000000000",
      "value": ["0x00"]
    },
    "post": {
      "Prague": [
        {
          "hash": "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "indexes": { "data": 0, "gas": 0, "value": 0 },
          "logs": "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        }
      ]
    }
  },
  "suite/case.json::variant-b": {
    "env": {
      "currentCoinbase": "0x2adc25665018aa1fe0e6bc666dac8fc2697ff9ba",
      "currentGasLimit": "0x0f4240",
      "currentNumber": "0x02",
      "currentTimestamp": "0x03e9"
    },
    "pre": {
      "0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b": {
        "balance": "0x0186a0",
        "code": "0x",
        "nonce": "0x00",
        "storage": {}
      }
    },
    "transaction": {
      "data": ["0x"],
      "gasLimit": ["0x5208"],
      "gasPrice": "0x0a",
      "nonce": "0x00",
      "secretKey": "0x00",
      "to": "0x1000000000000000000000000000000000000000",
      "value": ["0x00"]
    },
    "post": {
      "Prague": [
        {
          "hash": "0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
          "indexes": { "data": 0, "gas": 0, "value": 0 },
          "logs": "0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
        }
      ]
    }
  }
})";
    return path;
}
}  // namespace

BOOST_AUTO_TEST_CASE(loads_official_gst_json)
{
    auto const testCase = loadGeneralStateTest(sampleGstPath());
    BOOST_REQUIRE(!testCase.postByFork.empty());
    BOOST_CHECK(!testCase.name.empty());
    BOOST_CHECK_EQUAL(testCase.variantKey, testCase.name);
    BOOST_CHECK_GT(testCase.env.gasLimit, 0);
    BOOST_CHECK(!testCase.transaction.gasLimit.empty());
    BOOST_CHECK(!testCase.preState.empty());
}

BOOST_AUTO_TEST_CASE(lists_and_selects_prague_subtests)
{
    auto const testCase = loadGeneralStateTest(sampleGstPath());
    auto const subtests = listSubtests(testCase, "Prague");
    BOOST_REQUIRE(!subtests.empty());

    auto const expected = selectExpected(testCase, subtests.front());
    BOOST_CHECK(expected.stateRoot.has_value());
    BOOST_CHECK(expected.logsHash.has_value());
}

BOOST_AUTO_TEST_CASE(loads_all_map_variants_from_file)
{
    auto const path = writeMultiVariantFixture();
    auto const keys = listGeneralStateTestVariantKeys(path);
    BOOST_REQUIRE_EQUAL(keys.size(), 2u);
    BOOST_CHECK_EQUAL(keys[0], "suite/case.json::variant-a");
    BOOST_CHECK_EQUAL(keys[1], "suite/case.json::variant-b");

    auto const cases = loadGeneralStateTestFile(path);
    BOOST_REQUIRE_EQUAL(cases.size(), 2u);
    BOOST_CHECK_EQUAL(cases[0].variantKey, "suite/case.json::variant-a");
    BOOST_CHECK_EQUAL(cases[1].variantKey, "suite/case.json::variant-b");
    BOOST_CHECK(cases[0].env.number != cases[1].env.number);

    std::filesystem::remove(path);
}

BOOST_AUTO_TEST_CASE(requires_variant_key_for_multi_variant_map)
{
    auto const path = writeMultiVariantFixture();
    BOOST_CHECK_THROW(loadGeneralStateTest(path), std::runtime_error);

    auto const selected =
        loadGeneralStateTest(path, std::string_view{"suite/case.json::variant-b"});
    BOOST_CHECK_EQUAL(selected.variantKey, "suite/case.json::variant-b");
    BOOST_CHECK_EQUAL(selected.env.number, 2);

    std::filesystem::remove(path);
}

BOOST_AUTO_TEST_CASE(unsupported_format_returns_status_not_throw)
{
#ifdef SPECS_TESTS_SOURCE_DIR
    auto const path =
        std::filesystem::path(SPECS_TESTS_SOURCE_DIR) / "assets/eest/unsupported/not_gst.json";
#else
    auto const path =
        std::filesystem::path("bcos-evm/test/eth-eest-test/assets/eest/unsupported/not_gst.json");
#endif
    BOOST_REQUIRE(std::filesystem::exists(path));

    auto const result = tryLoadGeneralStateTestFile(path);
    BOOST_CHECK_EQUAL(
        static_cast<int>(result.status), static_cast<int>(StateTestLoadStatus::UnsupportedFormat));
    BOOST_CHECK(result.cases.empty());
    BOOST_CHECK(!result.reason.empty());
    BOOST_CHECK_THROW(loadGeneralStateTestFile(path), std::runtime_error);
}

}  // namespace bcos::evm::reference_tests
