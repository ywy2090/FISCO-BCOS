#define BOOST_TEST_MODULE Eip4844BlobBaseFeeTest
#include "bcos-evm/eth/eip/Eip4844.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using bcos::evm::gas::calcBlobBaseFee;

BOOST_AUTO_TEST_SUITE(Eip4844BlobBaseFeeTest)

BOOST_AUTO_TEST_CASE(calcBlobBaseFee_matches_geth_fake_exponential)
{
    BOOST_CHECK_EQUAL(calcBlobBaseFee(0), 1U);
    BOOST_CHECK_EQUAL(calcBlobBaseFee(0x0e0000), 1U);
    BOOST_CHECK_EQUAL(calcBlobBaseFee(0x080000), 1U);
    BOOST_CHECK_EQUAL(calcBlobBaseFee(1'542'706), 1U);
    BOOST_CHECK_EQUAL(calcBlobBaseFee(3'338'477), 2U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
