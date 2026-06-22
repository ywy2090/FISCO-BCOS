#define BOOST_TEST_MODULE Eip7702StrictTxValidatorTest
#include "bcos-evm/evm-reference-tests/Eip7702StrictTxValidator.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::reference_tests
{

BOOST_AUTO_TEST_CASE(rejects_empty_envelope)
{
    bcos::bytes empty;
    BOOST_CHECK(!validateStrictEip7702TypedTx(bcos::bytesConstRef{empty.data(), empty.size()}));
}

BOOST_AUTO_TEST_CASE(rejects_legacy_tx_prefix)
{
    auto const bytes = bcos::fromHex("0x01");
    BOOST_CHECK(!validateStrictEip7702TypedTx(bcos::bytesConstRef{bytes.data(), bytes.size()}));
}

}  // namespace bcos::evm::reference_tests
