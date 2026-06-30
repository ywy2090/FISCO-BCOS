#define BOOST_TEST_MODULE TxFeaturePrepareTest

#include "bcos-evm/eth/kernel/execution/TxFeaturePrepare.h"
#include <boost/test/included/unit_test.hpp>

using namespace bcos::evm;

BOOST_AUTO_TEST_SUITE(TxFeaturePrepareTest)

BOOST_AUTO_TEST_CASE(setWarmDestinationFromKind_matches_create_vs_call)
{
    state::TransactionProperties props;
    execution::setWarmDestinationFromKind(props, EVMC_CALL);
    BOOST_CHECK(props.warmDestination);

    execution::setWarmDestinationFromKind(props, EVMC_CREATE);
    BOOST_CHECK(!props.warmDestination);

    execution::setWarmDestinationFromKind(props, EVMC_CREATE2);
    BOOST_CHECK(!props.warmDestination);
}

BOOST_AUTO_TEST_SUITE_END()
