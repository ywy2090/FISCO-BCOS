#define BOOST_TEST_MODULE IsthmusPostExecutionPolicyTest

#include "bcos-evm/eth/RevisionConfig.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(isthmus_revision_config_disables_prague_post_execution)
{
    auto const config = bcos::evm_standard::makeIsthmusRevisionConfig();
    BOOST_CHECK(config.eip7702);
    BOOST_CHECK(config.eip7623);
    BOOST_CHECK(!config.prague_post_execution);
}
}  // namespace bcos::evm::test
