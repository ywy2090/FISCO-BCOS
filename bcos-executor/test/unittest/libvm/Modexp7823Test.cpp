/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-7823 modexp input length bounds (Osaka+).
 *  @file Modexp7823Test.cpp
 */

#include "vm/ModexpGas.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Modexp7823Test)

BOOST_AUTO_TEST_CASE(parse_lengths_small)
{
    bytes input(96, 0);
    input[31] = 32;
    input[63] = 1;
    input[95] = 32;
    auto const lens = executor::parseModexpLengths(ref(input));
    BOOST_CHECK(!lens.overflow);
    BOOST_CHECK_EQUAL(lens.baseLen, 32u);
    BOOST_CHECK_EQUAL(lens.expLen, 1u);
    BOOST_CHECK_EQUAL(lens.modLen, 32u);
}

BOOST_AUTO_TEST_CASE(parse_lengths_uint64_overflow)
{
    bytes input(96, 0);
    input[0] = 0xff;
    input[1] = 0xff;
    input[2] = 0xff;
    input[3] = 0xff;
    input[4] = 0xff;
    input[5] = 0xff;
    input[6] = 0xff;
    input[7] = 0xff;
    input[8] = 0x01;
    auto const lens = executor::parseModexpLengths(ref(input));
    BOOST_CHECK(lens.overflow);
}

BOOST_AUTO_TEST_CASE(validate_1024_ok_osaka)
{
    bytes input(96, 0);
    input[30] = 4;  // baseLen = 1024 (0x400 BE)
    BOOST_CHECK(executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(validate_1025_fail_osaka)
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;  // baseLen = 1025
    BOOST_CHECK(!executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(validate_1025_modlen0_fail_osaka)
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    input[95] = 0;
    BOOST_CHECK(!executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(validate_1025_ok_prague)
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    BOOST_CHECK(executor::validateModexpEip7823(ref(input), EVMC_PRAGUE));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
