#include "bcos-evm/eth/policy/EthChainPolicy.h"
#include "bcos-evm/eth/policy/EthForkSchedule.h"
#include "bcos-evm/eth/policy/EthMainnetRevision.h"
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::evm;

BOOST_AUTO_TEST_SUITE(EthChainPolicyTest)

BOOST_AUTO_TEST_CASE(evmcRevisionFromBlockNumberMainnetForks)
{
    BOOST_CHECK_EQUAL(evmcRevisionFromBlockNumber(ETH_MAINNET_PARIS_BLOCK - 1), EVMC_LONDON);
    BOOST_CHECK_EQUAL(evmcRevisionFromBlockNumber(ETH_MAINNET_PARIS_BLOCK), EVMC_PARIS);
    BOOST_CHECK_EQUAL(evmcRevisionFromBlockNumber(ETH_MAINNET_CANCUN_BLOCK), EVMC_CANCUN);
    BOOST_CHECK_EQUAL(evmcRevisionFromBlockNumber(ETH_MAINNET_PRAGUE_BLOCK), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(evmcRevisionFromBlockNumber(ETH_MAINNET_OSAKA_BLOCK), EVMC_OSAKA);
}

BOOST_AUTO_TEST_CASE(computeRevisionConfigPrague)
{
    EthChainPolicy policy;
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
    EthChainPolicy policy;
    bcostars::protocol::BlockHeaderImpl header(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    header.setNumber(13000000);

    auto rev = policy.computeRevisionConfig(header);

    BOOST_CHECK(rev.revision >= EVMC_LONDON);
    BOOST_CHECK(!rev.eip7623);
    BOOST_CHECK(!rev.eip2537);
    BOOST_CHECK(rev.calldata_floor_per_token == 0);
}

BOOST_AUTO_TEST_CASE(makeEthRevisionConfigFromBlockMatchesPolicy)
{
    bcostars::protocol::BlockHeaderImpl header(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    header.setNumber(ETH_MAINNET_CANCUN_BLOCK);

    EthChainPolicy policy;
    auto const fromHelper = makeEthRevisionConfigFromBlock(header);
    auto const fromPolicy = policy.computeRevisionConfig(header);
    BOOST_CHECK(fromHelper.revision == fromPolicy.revision);
    BOOST_CHECK(fromHelper.eip4844 == fromPolicy.eip4844);
    BOOST_CHECK(fromHelper.eip7702 == fromPolicy.eip7702);
}

BOOST_AUTO_TEST_CASE(convertTimestamp)
{
    EthChainPolicy policy;
    BOOST_CHECK_EQUAL(policy.convertTimestamp(1000), 1);
    BOOST_CHECK_EQUAL(policy.convertTimestamp(1500), 1);
    BOOST_CHECK_EQUAL(policy.convertTimestamp(0), 0);
}

BOOST_AUTO_TEST_SUITE_END()
