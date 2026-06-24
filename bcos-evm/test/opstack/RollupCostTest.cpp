#define BOOST_TEST_MODULE RollupCostTest

#include "bcos-evm/opstack/fee/RollupCost.h"
#include <boost/algorithm/hex.hpp>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
bytes fromHex(std::string_view hex)
{
    bytes out;
    out.reserve(hex.size() / 2);
    boost::algorithm::unhex(hex.begin(), hex.end(), std::back_inserter(out));
    return out;
}

// op-geth emptyTx.MarshalBinary() — legacy tx with zero gas/price, fixed to-address.
bytes const kEmptyTxBytes = fromHex("dd80808094095e7baea6a6c7c4c2dfeb977efac326af552d878080808080");

bytes const kContractCallTxBytes = fromHex(
    "02f901550a758302df1483be21b88304743f94f8"
    "0e51afb613d764fa61751affd3313c190a86bb870151bd62fd12adb8"
    "e41ef24f3f0000000000000000000000000000000000000000000000"
    "00000000000000006e000000000000000000000000af88d065e77c8c"
    "c2239327c5edb3a432268e5831000000000000000000000000000000"
    "000000000000000000000000000003c1e50000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000a0000000"
    "00000000000000000000000000000000000000000000000000000000"
    "148c89ed219d02f1a5be012c689b4f5b731827bebe00000000000000"
    "0000000000c001a033fd89cb37c31b2cba46b6466e040c61fc9b2a36"
    "75a7f5f493ebd5ad77c497f8a07cdf65680e238392693019b4092f61"
    "0222e71b7cec06449cb922b93b6a12744e");
bytesConstRef toRef(bytes const& data)
{
    return {data.data(), data.size()};
}
}  // namespace

BOOST_AUTO_TEST_CASE(FlzCompressLen_matchesOpGethVectors)
{
    {
        bytes empty;
        BOOST_CHECK_EQUAL(flzCompressLen(toRef(empty)), 0U);
    }
    {
        bytes ones(1000, 0x01);
        BOOST_CHECK_EQUAL(flzCompressLen(toRef(ones)), 21U);
    }
    {
        bytes zeroes(1000, 0x00);
        BOOST_CHECK_EQUAL(flzCompressLen(toRef(zeroes)), 21U);
    }
    {
        BOOST_CHECK_EQUAL(flzCompressLen(toRef(kEmptyTxBytes)), 31U);
    }
    {
        BOOST_CHECK_EQUAL(flzCompressLen(toRef(kContractCallTxBytes)), 202U);
    }
}

BOOST_AUTO_TEST_CASE(NewRollupCostData_countsBytesAndFastLz)
{
    auto const costData = newRollupCostData(toRef(kEmptyTxBytes));
    // RLP empty scalars encode as 0x80, which counts as "ones" in op-geth.
    BOOST_CHECK_EQUAL(costData.zeroes, 0U);
    BOOST_CHECK_EQUAL(costData.ones, 30U);
    BOOST_CHECK_EQUAL(costData.fastLzSize, 31U);
    BOOST_CHECK(!costData.isEmpty());
}

}  // namespace bcos::evm::test
