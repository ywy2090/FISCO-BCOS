#define BOOST_TEST_MODULE EstimatedDASizeTest

#include "bcos-evm/opstack/fee/RollupCost.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
// op-geth EstimatedDASize = max(MIN_TX_SIZE_SCALED, INTERCEPT + FASTLZ_COEF*fastLzSize) / 1e6
// MIN_TX_SIZE_SCALED = 100'000'000; INTERCEPT = -42'585'600; FASTLZ_COEF = 836'500
BOOST_AUTO_TEST_CASE(small_fastlz_floors_to_min)
{
    // fastLzSize=0 -> intercept negative -> floored to MIN -> 100'000'000/1e6 = 100
    BOOST_CHECK_EQUAL(estimatedDASize(RollupCostData{.fastLzSize = 0}), 100u);
    // fastLzSize=64 -> -42'585'600 + 53'536'000 = 10'950'400 < MIN -> 100
    BOOST_CHECK_EQUAL(estimatedDASize(RollupCostData{.fastLzSize = 64}), 100u);
}

BOOST_AUTO_TEST_CASE(large_fastlz_exceeds_min)
{
    // fastLzSize=200 -> 836'500*200 - 42'585'600 = 124'714'400 -> /1e6 = 124
    BOOST_CHECK_EQUAL(estimatedDASize(RollupCostData{.fastLzSize = 200}), 124u);
}

BOOST_AUTO_TEST_CASE(scaled_is_not_divided)
{
    // scaled 版本保留 1e6 放大（供 l1CostFjord 复用），fastLzSize=200 -> 124'714'400
    BOOST_CHECK_EQUAL(estimatedDASizeScaled(RollupCostData{.fastLzSize = 200}), s256(124'714'400));
    // 负中间量被 MIN 兜底（验证有符号，防 uint64 underflow）
    BOOST_CHECK_EQUAL(estimatedDASizeScaled(RollupCostData{.fastLzSize = 0}), s256(100'000'000));
}
}  // namespace bcos::evm::test
