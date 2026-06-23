#define BOOST_TEST_MODULE Eip7702StrictTxValidatorTest
#include "bcos-evm/specs-tests/Eip7702StrictTxValidator.h"
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

BOOST_AUTO_TEST_CASE(rejects_non_canonical_auth_chain_id_encoding)
{
    auto const bytes = bcos::fromHex(
        "04f8c301808007830186a09400000000000000000000000000000000000000008080c0f85ef85c8094000000"
        "00000000000000000000000000000000000182000080a083fa55138a74c229c5508173575054bff977155da0"
        "d708b6c8c1150b4c140238a0605878ffcbcbc76fa46a7ff477c865eccda27bf4dc44a6a4f4857e35ede15a9"
        "080a0701eb8238974d2c76e721b42b5d667cbf3b9b5756006c472c562c7c8ada19333a0030861b06c15251b"
        "166ebca7a2a03c307a5bcccd1ae0a77d344bbb77de796c10");
    BOOST_CHECK(!validateStrictEip7702TypedTx(bcos::bytesConstRef{bytes.data(), bytes.size()}));
}

}  // namespace bcos::evm::reference_tests
