#define BOOST_TEST_MODULE OpStackTxPropsTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
#include <boost/test/included/unit_test.hpp>

using namespace bcos::evm;

BOOST_AUTO_TEST_SUITE(OpStackTxPropsTest)

BOOST_AUTO_TEST_CASE(isthmus_revision_profile_enables_eip2929)
{
    auto const config = bcos::evm::makeIsthmusRevisionConfig();
    BOOST_CHECK(config.eip2929);
    BOOST_CHECK_EQUAL(config.revision, EVMC_PRAGUE);
}

BOOST_AUTO_TEST_SUITE_END()
