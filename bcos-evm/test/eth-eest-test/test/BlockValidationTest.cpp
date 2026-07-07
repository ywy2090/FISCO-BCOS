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

BOOST_AUTO_TEST_CASE(excess_blob_gas_zero_below_target)
{
    // Cancun target=3 -> TARGET_BLOB_GAS = 3*131072 = 393216.
    // parentExcess=0, parentUsed=131072 (1 blob) -> sum < target -> 0
    BlobSchedule sched;  // empty -> blobParamsFor returns Cancun defaults
    BOOST_CHECK_EQUAL(calcExcessBlobGas(EVMC_CANCUN, sched, 131072, 0), 0ull);
}

BOOST_AUTO_TEST_CASE(excess_blob_gas_accumulates_above_target)
{
    // parentUsed = 6 blobs = 786432, parentExcess=0, target gas=393216
    // -> 786432 + 0 - 393216 = 393216
    BlobSchedule sched;
    BOOST_CHECK_EQUAL(calcExcessBlobGas(EVMC_CANCUN, sched, 786432, 0), 393216ull);
}

BOOST_AUTO_TEST_CASE(blob_gas_price_min_at_zero_excess)
{
    BlobParams p;                                        // Cancun defaults
    BOOST_CHECK_EQUAL(computeBlobGasPrice(p, 0), 1ull);  // MIN_BLOB_BASE_FEE
}

BOOST_AUTO_TEST_CASE(rejects_wrong_excess_blob_gas)
{
    TestBlockHeader parent;
    parent.blockNumber = 0;
    parent.gasLimit = 20000000;
    parent.timestamp = 0;
    parent.baseFeePerGas = 7;
    parent.gasUsed = 0;
    parent.blobGasUsed = 786432;
    parent.excessBlobGas = 0;  // -> expected excess 393216
    TestBlock tb;
    auto& h = tb.expectedBlockHeader;
    h.blockNumber = 1;
    h.gasLimit = 20000000;
    h.timestamp = 1;
    h.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    h.blobGasUsed = 0;
    h.excessBlobGas = 999999;  // wrong
    tb.inputBlobGasUsed = 0;
    tb.inputExcessBlobGas = 999999;
    auto err = validateBlock(EVMC_CANCUN, {}, tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::INCORRECT_EXCESS_BLOB_GAS));
}

BOOST_AUTO_TEST_CASE(rejects_oversized_rlp_block_on_osaka)
{
    TestBlockHeader parent;
    parent.blockNumber = 0;
    parent.gasLimit = 20000000;
    parent.timestamp = 0;
    parent.baseFeePerGas = 7;
    parent.gasUsed = 0;
    parent.blobGasUsed = 0;
    parent.excessBlobGas = 0;
    TestBlock tb;
    auto& h = tb.expectedBlockHeader;
    h.blockNumber = 1;
    h.gasLimit = 20000000;
    h.timestamp = 1;
    h.baseFeePerGas = calcBaseFee(20000000, 0, 7);
    h.blobGasUsed = 0;
    h.excessBlobGas = 0;
    tb.inputBlobGasUsed = 0;
    tb.inputExcessBlobGas = 0;
    tb.rlpSize = 9 * 1024 * 1024;  // > 8MB
    auto err = validateBlock(EVMC_OSAKA, {}, tb, &parent);
    BOOST_REQUIRE(err.has_value());
    BOOST_CHECK_EQUAL(*err, std::string(BlockError::RLP_BLOCK_LIMIT_EXCEEDED));
}
}  // namespace bcos::evm::reference_tests
