#include "bcos-evm/eth/policy/EthPolicy.h"
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::evm_standard;

BOOST_AUTO_TEST_SUITE(EthPolicyTest)

BOOST_AUTO_TEST_CASE(computeRevisionConfigPrague)
{
    EthPolicy policy;
    bcostars::protocol::BlockHeaderImpl header(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    header.setNumber(23000000);

    auto rev = policy.computeRevisionConfig(header);

    BOOST_CHECK(rev.revision >= EVMC_PRAGUE);
    BOOST_CHECK(rev.eip7623);
    BOOST_CHECK(rev.eip2537);
    BOOST_CHECK(rev.eip1153);
    BOOST_CHECK(rev.eip4844);
    BOOST_CHECK(rev.eip5656);
    BOOST_CHECK(rev.eip6780);
    BOOST_CHECK(!rev.eip7212);
    BOOST_CHECK(!rev.eip7823);
    BOOST_CHECK_EQUAL(rev.calldata_floor_per_token, 10);
}

BOOST_AUTO_TEST_CASE(computeRevisionConfigLondon)
{
    EthPolicy policy;
    bcostars::protocol::BlockHeaderImpl header(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    header.setNumber(13000000);

    auto rev = policy.computeRevisionConfig(header);

    BOOST_CHECK(rev.revision >= EVMC_LONDON);
    BOOST_CHECK(!rev.eip7623);
    BOOST_CHECK(!rev.eip2537);
    BOOST_CHECK(rev.calldata_floor_per_token == 0);
}

BOOST_AUTO_TEST_CASE(allowDelegateCallToPrecompile)
{
    EthPolicy policy;
    BOOST_CHECK(policy.allowDelegateCallToPrecompile());
}

BOOST_AUTO_TEST_CASE(convertTimestamp)
{
    EthPolicy policy;
    BOOST_CHECK_EQUAL(policy.convertTimestamp(1000), 1);
    BOOST_CHECK_EQUAL(policy.convertTimestamp(1500), 1);
    BOOST_CHECK_EQUAL(policy.convertTimestamp(0), 0);
}

BOOST_AUTO_TEST_SUITE_END()
