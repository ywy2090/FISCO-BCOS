#define BOOST_TEST_MODULE OpStackTxPropsTest

#include "../../../transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/opstack/OpStackIsthmusRevision.h"
#include <boost/test/included/unit_test.hpp>

using namespace bcos::evm;

BOOST_AUTO_TEST_SUITE(OpStackTxPropsTest)

BOOST_AUTO_TEST_CASE(applyDefaultTxProps_sets_warm_destination_from_kind)
{
    OpStackExecutionRequest input;
    input.message.kind = EVMC_CALL;
    opstack_tx::applyDefaultTxProps(input);
    BOOST_CHECK(input.txProps.warmDestination);

    input.message.kind = EVMC_CREATE;
    opstack_tx::applyDefaultTxProps(input);
    BOOST_CHECK(!input.txProps.warmDestination);
}

BOOST_AUTO_TEST_CASE(executor_build_order_clears_warm_destination_for_create)
{
    OpStackExecutionRequest input;
    input.message.kind = EVMC_CREATE;
    BOOST_CHECK(input.txProps.warmDestination);

    opstack_tx::applyDefaultTxProps(input);
    BOOST_CHECK(!input.txProps.warmDestination);
}

BOOST_AUTO_TEST_CASE(isthmus_revision_profile_enables_warm_access)
{
    auto const config = bcos::evm::makeIsthmusRevisionConfig();
    BOOST_CHECK(config.warm_access);
    BOOST_CHECK_EQUAL(config.revision, EVMC_PRAGUE);
}

BOOST_AUTO_TEST_SUITE_END()
