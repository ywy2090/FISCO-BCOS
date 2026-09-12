/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file ReceiptFieldBaselineTest.cpp
 * @brief M3: receipt scalar edges. The fork->meta mapping is asserted at the EXECUTION layer
 * (this file cannot see it: combineReceiptResponse is presence-driven, not fork-driven).
 */
#include "../common/RPCFixture.h"
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
bcos::protocol::TransactionReceipt::Ptr makeBaselineReceipt(
    bcos::protocol::BlockFactory::Ptr const& blockFactory)
{
    std::vector<bcos::protocol::LogEntry> logs;
    auto receipt = blockFactory->receiptFactory()->createReceipt(bcos::u256(21000),
        "0x1234567890123456789012345678901234567890", logs, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/12);
    receipt->setTransactionIndex(0);
    return receipt;
}

bcos::protocol::Transaction::Ptr makeBaselineTx(
    bcos::protocol::BlockFactory::Ptr const& blockFactory, std::string const& chainId,
    std::string const& groupId)
{
    return blockFactory->transactionFactory()->createTransaction(0,
        "0x1234567890123456789012345678901234567890", bcos::bytes{0x0a}, "0x2", 100, chainId,
        groupId, 0);
}

/// The emitted l1FeeScalar for a raw slot-6 scalar.
std::string emittedScalar(bcos::protocol::BlockFactory::Ptr const& blockFactory,
    std::string const& chainId, std::string const& groupId, bcos::u256 rawScalar)
{
    auto receipt = makeBaselineReceipt(blockFactory);
    auto tx = makeBaselineTx(blockFactory, chainId, groupId);
    protocol::OpStackReceiptMeta meta;
    meta.l1_gas_price = bcos::u256(1);
    meta.l1_fee = bcos::u256(3);
    meta.l1_fee_scalar = rawScalar;
    receipt->setOpStackMeta(std::move(meta));
    bcos::crypto::HashType blockHash;
    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *receipt, *tx, blockHash);
    BOOST_REQUIRE(result.isMember("l1FeeScalar"));
    return result["l1FeeScalar"].asString();
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(ReceiptFieldBaselineTest, RPCFixture)

/// Exact multiple of 1e6: op-geth's intToScaledFloat and this lane's integer division agree.
// clang-format off
BOOST_AUTO_TEST_CASE(L1FeeScalarExactMultipleIsUnscaled, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon"))
// clang-format on
{
    BOOST_CHECK_EQUAL(
        emittedScalar(m_blockFactory, chainId, groupId, bcos::u256(2'000'000)), "0x2");
}

/// Non-multiple: 2'000'001 / 1e6 truncates to 2 here, while op-geth's intToScaledFloat would
/// emit 2.000001. REGISTERED DEVIATION rpc_l1_fee_scalar_truncation
/// (opstack-executor/tests/da-matrix/DIVERGENCES.md): pinned so a change in the scaling rule
/// is caught, and so the deviation cannot silently disappear from the diff surface.
// clang-format off
BOOST_AUTO_TEST_CASE(L1FeeScalarNonMultipleTruncatesIsRegisteredDeviation, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon"))
// clang-format on
{
    BOOST_CHECK_EQUAL(
        emittedScalar(m_blockFactory, chainId, groupId, bcos::u256(2'000'001)), "0x2");
}

/// Below one unit: 999'999 / 1e6 == 0, i.e. the field is emitted as an explicit zero rather
/// than dropped. The empty-meta contract (no field) is a different case, already covered in
/// Web3ResponseTest.cpp:661-704.
// clang-format off
BOOST_AUTO_TEST_CASE(L1FeeScalarBelowOneUnitEmitsExplicitZero, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon"))
// clang-format on
{
    BOOST_CHECK_EQUAL(emittedScalar(m_blockFactory, chainId, groupId, bcos::u256(999'999)), "0x0");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
