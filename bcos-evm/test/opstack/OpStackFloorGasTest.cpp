#define BOOST_TEST_MODULE OpStackFloorGasTest

#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include <boost/test/included/unit_test.hpp>
#include <limits>

namespace bcos::evm::test
{
namespace
{
constexpr uint64_t kTxGas = 21'000;
constexpr uint64_t kTokenPerNonZero = 4;
constexpr uint64_t kCostFloorPerToken = 10;

bytesConstRef toRef(bytes const& data)
{
    return {data.data(), data.size()};
}
}  // namespace

BOOST_AUTO_TEST_CASE(FloorDataGas_emptyCalldata_is21000)
{
    bytes empty;
    BOOST_CHECK_EQUAL(floorDataGas(toRef(empty)), kTxGas);
}

BOOST_AUTO_TEST_CASE(FloorDataGas_zeroBytesOnly)
{
    bytes data(100, 0x00);
    BOOST_CHECK_EQUAL(floorDataGas(toRef(data)), kTxGas + 100 * kCostFloorPerToken);
}

BOOST_AUTO_TEST_CASE(FloorDataGas_nonZeroBytesOnly)
{
    bytes data(100, 0xff);
    BOOST_CHECK_EQUAL(
        floorDataGas(toRef(data)), kTxGas + 100 * kTokenPerNonZero * kCostFloorPerToken);
}

BOOST_AUTO_TEST_CASE(FloorDataGas_mixedZeroAndNonZero)
{
    bytes data(100);
    for (size_t i = 0; i < 50; ++i)
    {
        data[i] = 0x00;
    }
    for (size_t i = 50; i < 100; ++i)
    {
        data[i] = 0xff;
    }
    auto const tokens = 50 + 50 * kTokenPerNonZero;
    BOOST_CHECK_EQUAL(floorDataGas(toRef(data)), kTxGas + tokens * kCostFloorPerToken);
}

BOOST_AUTO_TEST_CASE(FloorDataGas_singleZeroByte)
{
    bytes data{0x00};
    BOOST_CHECK_EQUAL(floorDataGas(toRef(data)), kTxGas + kCostFloorPerToken);
}

BOOST_AUTO_TEST_CASE(FloorDataGas_singleNonZeroByte)
{
    bytes data{0x42};
    BOOST_CHECK_EQUAL(floorDataGas(toRef(data)), kTxGas + kTokenPerNonZero * kCostFloorPerToken);
}

BOOST_AUTO_TEST_CASE(FloorDataGas_overflow_returnsError)
{
    bytes empty;
    auto const maxTokens = (std::numeric_limits<uint64_t>::max() - kTxGas) / kCostFloorPerToken;
    auto const result = tryFloorDataGas(toRef(empty), maxTokens + 1);
    BOOST_CHECK(!static_cast<bool>(result));
    BOOST_REQUIRE(result.error.has_value());
    BOOST_CHECK(*result.error == FloorDataGasError::GasUintOverflow);
}

BOOST_AUTO_TEST_CASE(ExecuteEntryFloorCheck_gasLimitBelowFloor_rejects)
{
    bytes data(100, 0xff);
    auto const floor = floorDataGas(toRef(data));
    auto const check = executeEntryFloorDataGasCheck(floor - 1, toRef(data));
    BOOST_CHECK(!check.ok);
    BOOST_CHECK(check.error == ExecuteEntryFloorError::BelowFloor);
    BOOST_CHECK_EQUAL(check.floorGas, floor);
    BOOST_CHECK_EQUAL(check.gasLimit, floor - 1);
}

BOOST_AUTO_TEST_CASE(ExecuteEntryFloorCheck_gasLimitAtFloor_accepts)
{
    bytes data(100, 0xff);
    auto const floor = floorDataGas(toRef(data));
    auto const check = executeEntryFloorDataGasCheck(floor, toRef(data));
    BOOST_CHECK(check.ok);
    BOOST_CHECK_EQUAL(check.floorGas, floor);
}

}  // namespace bcos::evm::test
