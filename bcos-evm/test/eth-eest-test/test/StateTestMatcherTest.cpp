#define BOOST_TEST_MODULE StateTestMatcherTest
#include "bcos-evm/eth-eest-test/StateTestMatcher.h"
#include <boost/test/included/unit_test.hpp>
#include <filesystem>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(hard_skips_eof_and_time_consuming)
{
    StateTestMatcher matcher(std::filesystem::path(SPECS_TESTS_MANIFEST_DIR) / "expectations.json");

    auto const eofDecision = matcher.decide(
        "GeneralStateTests/stEOF/EOFCreateOrExtCreate.json", ExecutionPath::Reference);
    BOOST_REQUIRE(eofDecision.kind == MatchDecision::Kind::Skip);
    BOOST_REQUIRE(eofDecision.reason.has_value());

    auto const slowDecision =
        matcher.decide("GeneralStateTests/stTimeConsuming/slowCase.json", ExecutionPath::Reference);
    BOOST_REQUIRE(slowDecision.kind == MatchDecision::Kind::Skip);
}

BOOST_AUTO_TEST_CASE(default_is_run)
{
    StateTestMatcher matcher(std::filesystem::path(SPECS_TESTS_MANIFEST_DIR) / "expectations.json");
    auto const decision = matcher.decide(
        "GeneralStateTests/stSelfBalance/selfBalance.json", ExecutionPath::Reference);
    BOOST_CHECK(decision.kind == MatchDecision::Kind::Run);
}

}  // namespace bcos::evm::reference_tests
