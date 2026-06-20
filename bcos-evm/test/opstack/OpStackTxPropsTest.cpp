#define BOOST_TEST_MODULE OpStackTxPropsTest

#include "../../../transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h"
#include <boost/test/included/unit_test.hpp>

using namespace bcos::evm;

BOOST_AUTO_TEST_SUITE(OpStackTxPropsTest)

BOOST_AUTO_TEST_CASE(applyDefaultTxProps_sets_warm_destination_from_kind)
{
    OpStackExecuteViaHostInput input;
    input.message.kind = EVMC_CALL;
    opstack_tx::applyDefaultTxProps(input);
    BOOST_CHECK(input.txProps.warmDestination);

    input.message.kind = EVMC_CREATE;
    opstack_tx::applyDefaultTxProps(input);
    BOOST_CHECK(!input.txProps.warmDestination);
}

BOOST_AUTO_TEST_SUITE_END()
