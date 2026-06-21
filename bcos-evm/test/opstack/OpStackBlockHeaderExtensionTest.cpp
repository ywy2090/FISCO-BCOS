#define BOOST_TEST_MODULE OpStackBlockHeaderExtensionTest

#include "bcos-evm/opstack/OpStackBlockHeaderExtension.h"
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(encode_decode_round_trip)
{
    auto const coinbase = addressFromLastByte(0x42);
    auto const encoded = encodeOpStackHeaderExtra(coinbase, bcos::u256(5), 0);

    bcostars::protocol::BlockHeaderImpl header;
    header.setExtraData(std::move(encoded));

    auto const parsed = tryParseOpStackHeaderFees(header);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK_EQUAL(parsed->baseFee, bcos::u256(5));
    BOOST_CHECK_EQUAL(parsed->excessBlobGas, 0U);
    BOOST_CHECK_EQUAL(calcOpStackBlobBaseFee(parsed->excessBlobGas), OP_MIN_BLOB_GAS_PRICE);
}

BOOST_AUTO_TEST_CASE(missing_opf1_returns_nullopt)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setExtraData(bcos::bytes(20, 0x00));
    BOOST_CHECK(!tryParseOpStackHeaderFees(header).has_value());
}

BOOST_AUTO_TEST_CASE(require_throws_without_opf1)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setExtraData(bcos::bytes(20, 0x00));
    BOOST_CHECK_THROW(requireOpStackHeaderFees(header), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(non_zero_excess_blob_gas_throws)
{
    BOOST_CHECK_THROW(calcOpStackBlobBaseFee(1), std::invalid_argument);
}
}  // namespace bcos::evm::test
