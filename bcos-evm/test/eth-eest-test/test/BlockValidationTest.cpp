#define BOOST_TEST_MODULE BlockValidationTest
#include "helpers/BlockValidation.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{
BOOST_AUTO_TEST_CASE(base_fee_constant_when_gas_used_equals_target)
{
    // parentGasLimit=20000000 -> target=10000000; used==target -> unchanged
    BOOST_CHECK_EQUAL(calcBaseFee(20000000, 10000000, 1000000000), 1000000000ull);
}

BOOST_AUTO_TEST_CASE(base_fee_rises_when_over_target)
{
    // used=15,000,000 target=10,000,000 base=1e9 -> +1e9*5e6/1e7/8 = +62,500,000
    BOOST_CHECK_EQUAL(calcBaseFee(20000000, 15000000, 1000000000), 1062500000ull);
}

BOOST_AUTO_TEST_CASE(rejects_missing_parent)
{
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 1;
    auto err = validateBlock(EVMC_LONDON, {}, tb, /*parent*/ nullptr);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INVALID_BLOCK_PARENT));
}

BOOST_AUTO_TEST_CASE(rejects_non_sequential_number)
{
    TestBlockHeader parent;
    parent.blockNumber = 5;
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 7;  // must be 6
    tb.expectedBlockHeader.gasLimit = 20000000;
    tb.expectedBlockHeader.timestamp = 100;
    parent.gasLimit = 20000000;
    parent.timestamp = 50;
    auto err = validateBlock(EVMC_PARIS, {}, tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INVALID_BLOCK_NUMBER));
}

BOOST_AUTO_TEST_CASE(rejects_ommers_on_paris_plus)
{
    TestBlockHeader parent;
    parent.blockNumber = 0;
    parent.gasLimit = 20000000;
    parent.timestamp = 0;
    parent.baseFeePerGas = 7;
    parent.gasUsed = 0;
    TestBlock tb;
    tb.expectedBlockHeader.blockNumber = 1;
    tb.expectedBlockHeader.gasLimit = 20000000;
    tb.expectedBlockHeader.timestamp = 1;
    tb.expectedBlockHeader.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    tb.hasOmmers = true;
    auto err = validateBlock(EVMC_PARIS, {}, tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INCORRECT_BLOCK_FORMAT));
}
}  // namespace bcos::evm::reference_tests
