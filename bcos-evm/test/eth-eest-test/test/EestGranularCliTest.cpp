#define BOOST_TEST_MODULE EestGranularCliTest
#include "bcos-evm/eth-eest-test/EestGranularCli.h"
#include <boost/test/included/unit_test.hpp>

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

}  // namespace bcos::evm::reference_tests
