/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "bcos-framework/ledger/OpForkScheduleMigration.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::ledger;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(OpForkScheduleMigrateTest)

BOOST_AUTO_TEST_CASE(acceptsFutureOnlyKarstExtension)
{
    BOOST_CHECK_NO_THROW(validateOpForkScheduleMigration("0:jovian", "0:jovian,1781712001:karst",
        /*safeHeadSeconds=*/1780000000));
}

BOOST_AUTO_TEST_CASE(rejectsHistoryRewrite)
{
    BOOST_CHECK_THROW(validateOpForkScheduleMigration(
                          "0:jovian", "0:isthmus,1:jovian,1781712001:karst", 1780000000),
        InvalidOpForkScheduleMigration);
}

BOOST_AUTO_TEST_CASE(rejectsPastKarstActivation)
{
    BOOST_CHECK_THROW(
        validateOpForkScheduleMigration("0:jovian", "0:jovian,1770000000:karst", 1780000000),
        InvalidOpForkScheduleMigration);
}

BOOST_AUTO_TEST_CASE(rejectsReverseMigrationAfterKarstActive)
{
    BOOST_CHECK_THROW(validateOpForkScheduleMigration(
                          "0:isthmus,1764691201:jovian,1783526401:karst", "0:jovian", 1784000000),
        InvalidOpForkScheduleMigration);
}

BOOST_AUTO_TEST_CASE(idempotentNoOp)
{
    BOOST_CHECK_NO_THROW(validateOpForkScheduleMigration(
        "0:jovian,1781712001:karst", "0:jovian,1781712001:karst", 1780000000));
}

BOOST_AUTO_TEST_CASE(millisecondBoundaryMapsToSeconds)
{
    constexpr uint64_t safeHeadSeconds = 1780000000;
    BOOST_CHECK_EQUAL(opForkScheduleSafeHeadSeconds(safeHeadSeconds * 1000 + 999), safeHeadSeconds);
    BOOST_CHECK_EQUAL(
        opForkScheduleSafeHeadSeconds((safeHeadSeconds + 1) * 1000), safeHeadSeconds + 1);

    BOOST_CHECK_NO_THROW(validateOpForkScheduleMigration("0:jovian", "0:jovian,1781712001:karst",
        opForkScheduleSafeHeadSeconds(safeHeadSeconds * 1000 + 999)));
    BOOST_CHECK_THROW(validateOpForkScheduleMigration("0:jovian", "0:jovian,1770000000:karst",
                          opForkScheduleSafeHeadSeconds(safeHeadSeconds * 1000 + 999)),
        InvalidOpForkScheduleMigration);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
