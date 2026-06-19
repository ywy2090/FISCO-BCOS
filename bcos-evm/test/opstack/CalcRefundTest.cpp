#define BOOST_TEST_MODULE CalcRefundTest

#include "bcos-evm/opstack/OpStackGasSettlement.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(Settlement_capBinds)
{
    auto const settlement = postExecuteGasSettlement(100'000, 80'000, 50'000, 0);
    BOOST_CHECK_EQUAL(settlement.refund, 4'000u);
    BOOST_CHECK_EQUAL(settlement.gasRemaining, 84'000u);
    BOOST_CHECK_EQUAL(settlement.gasUsed, 16'000u);
    BOOST_CHECK_EQUAL(settlement.maxUsedGas, 20'000u);
}

BOOST_AUTO_TEST_CASE(Settlement_floorDataGasBumpsGasUsed)
{
    auto const settlement = postExecuteGasSettlement(1'618, 1'118, 0, 700);
    BOOST_CHECK_EQUAL(settlement.gasUsed, 700u);
    BOOST_CHECK_EQUAL(settlement.gasRemaining, 918u);
    BOOST_CHECK_EQUAL(settlement.maxUsedGas, 700u);
}

BOOST_AUTO_TEST_CASE(EvmoneParity_noDoubleCount)
{
    // evmone reports gas_left before host refund accounting.
    auto const settlement = postExecuteGasSettlement(50'000, 20'000, 30'000, 0);
    BOOST_CHECK_EQUAL(settlement.gasLeft, 20'000u);
    BOOST_CHECK_EQUAL(settlement.refund, 6'000u);
    BOOST_CHECK_EQUAL(settlement.gasRemaining, 26'000u);
    BOOST_CHECK_EQUAL(settlement.gasUsed, 24'000u);
    BOOST_CHECK_EQUAL(settlement.maxUsedGas, 30'000u);
}
}  // namespace bcos::evm::test
