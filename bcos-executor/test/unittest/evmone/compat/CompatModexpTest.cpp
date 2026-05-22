/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-M*: modexp (EIP-198) edge cases vs legacy behaviour.
 *  @file CompatModexpTest.cpp
 */

#include "CompatTestFixture.h"
#include "vm/ModexpGas.h"
#include "vm/Precompiled.h"
#include <Common.h>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::test
{
using compat::compatMakeModexpInput;

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatModexp)

BOOST_AUTO_TEST_CASE(FC_M_modexp_compatibility)
{
    // Small vector from legacy ForwardCompatibility / EvmPrecompiledTest: 2^8 mod 10 = 6
    auto run = [](bytes const& input) {
        return executor::PrecompiledRegistrar::executor("modexp")(ref(input));
    };
    auto r = run(compatMakeModexpInput({0x02}, {0x08}, {0x0a}));
    BOOST_REQUIRE(r.first);
    BOOST_CHECK_EQUAL(r.second[0], 6);
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_7_pow_0_mod_11)
{
    bytes input(96, 0);
    input[31] = 1;
    input[63] = 1;
    input[95] = 1;
    input.push_back(0x07);
    input.push_back(0x00);
    input.push_back(0x0B);
    auto result = executor::PrecompiledRegistrar::executor("modexp")(ref(input));
    BOOST_CHECK(result.first);
    BOOST_REQUIRE_EQUAL(result.second.size(), 1u);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(result.second[0]), 1u);
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_mod_zero_empty)
{
    bytes inputZeroMod(96, 0);
    inputZeroMod[31] = 1;
    inputZeroMod[63] = 1;
    inputZeroMod[95] = 0;
    inputZeroMod.push_back(0x02);
    inputZeroMod.push_back(0x03);
    auto resultZeroMod = executor::PrecompiledRegistrar::executor("modexp")(ref(inputZeroMod));
    BOOST_CHECK(resultZeroMod.first);
    BOOST_CHECK(resultZeroMod.second.empty());
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_large_boundary)
{
    // EvmPrecompiledTest::modexpCompatibility — 7-byte modulus output (all zeros)
    bytes shortInput(96, 0);
    shortInput[31] = 1;
    shortInput[63] = 1;
    shortInput[95] = 7;
    auto r = executor::PrecompiledRegistrar::executor("modexp")(ref(shortInput));
    BOOST_REQUIRE(r.first);
    BOOST_CHECK_EQUAL(r.second.size(), 7u);
    BOOST_CHECK(r.second == bytes(7, 0));
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_gas_berlin_vs_istanbul)
{
    bytes input(96, 0);
    input[31] = 32;
    input[63] = 1;
    input[95] = 32;
    input.resize(96 + 32 + 1 + 32, 0);
    input[96] = 0x01;
    input[96 + 32] = 0x01;
    input[96 + 33] = 0x01;

    auto const gas198 = executor::calcModexpGas(ref(input), EVMC_ISTANBUL).convert_to<int64_t>();
    auto const gas2565 = executor::calcModexpGas(ref(input), EVMC_BERLIN).convert_to<int64_t>();
    BOOST_CHECK_EQUAL(gas198, 51);
    BOOST_CHECK_EQUAL(gas2565, 200);
    BOOST_CHECK_NE(gas198, gas2565);
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_gas_osaka_7883_min)
{
    bytes input(96, 0);
    input[31] = 1;
    input[63] = 1;
    input[95] = 1;
    input.push_back(0x03);
    input.push_back(0x01);
    input.push_back(0x01);
    BOOST_CHECK_EQUAL(executor::calcModexpGas(ref(input), EVMC_OSAKA).convert_to<int64_t>(), 500);
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_eip7823_header_reject)
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    BOOST_CHECK(!executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
    BOOST_CHECK(executor::validateModexpEip7823(ref(input), EVMC_PRAGUE));
}

BOOST_AUTO_TEST_SUITE_END()  // CompatModexp
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
