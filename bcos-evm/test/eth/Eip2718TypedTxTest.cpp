#define BOOST_TEST_MODULE Eip2718TypedTxTest
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_SUITE(Eip2718TypedTxTest)

BOOST_AUTO_TEST_CASE(infers_type1_from_access_list)
{
    BOOST_CHECK_EQUAL(
        inferWeb3TypedTxKindFromFields(false, false, false, false, false, true), 0x01);
}

BOOST_AUTO_TEST_CASE(infers_type2_from_fee_caps)
{
    BOOST_CHECK_EQUAL(
        inferWeb3TypedTxKindFromFields(false, false, false, false, true, false), 0x02);
}

BOOST_AUTO_TEST_CASE(infers_type3_from_blob_hashes)
{
    BOOST_CHECK_EQUAL(inferWeb3TypedTxKindFromFields(false, false, true, false, true, true), 0x03);
}

BOOST_AUTO_TEST_CASE(infers_type3_from_max_fee_per_blob_gas_key)
{
    BOOST_CHECK_EQUAL(inferWeb3TypedTxKindFromFields(false, false, false, true, true, true), 0x03);
}

BOOST_AUTO_TEST_CASE(infers_type4_from_auth_list_key)
{
    BOOST_CHECK_EQUAL(
        inferWeb3TypedTxKindFromFields(true, false, false, false, false, false), 0x04);
}

BOOST_AUTO_TEST_CASE(legacy_when_no_typed_fields)
{
    BOOST_CHECK_EQUAL(inferWeb3TypedTxKindFromFields(false, false, false, false, false, false), 0);
}

// geth L1 parity: deposit type 0x7E is OP Stack-only; the shared eth/ gate rejects it
// on every revision (ErrTxTypeNotSupported). OP Stack paths accept 0x7E in their own layer.
BOOST_AUTO_TEST_CASE(opstack_deposit_rejected_on_eth_l1)
{
    bcos::evm::RevisionConfig london{};
    london.revision = EVMC_LONDON;
    BOOST_CHECK(!isTypedTxKindSupportedByRevision(
        toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit), london));

    auto const osaka = revisionConfigFromRevision(EVMC_OSAKA);
    BOOST_CHECK(!isTypedTxKindSupportedByRevision(
        toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit), osaka));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
