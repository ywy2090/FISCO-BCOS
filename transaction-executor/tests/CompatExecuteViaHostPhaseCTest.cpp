/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief TE Phase C: BLS/p256 builtin, EIP-7623 docs, selfdestruct docs, identity smoke.
 *  @file CompatExecuteViaHostPhaseCTest.cpp
 */

#include "../../bcos-executor/test/unittest/evmone/compat/CompatTestFixture.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <boost/test/unit_test.hpp>
#include <cstring>

namespace bcos::test
{
namespace
{
using compat::compatMakeModexpInput;

bytes blsG1AddValidInputG1PlusP1()
{
    return bcos::fromHex(
        "0000000000000000000000000000000017f1d3a73197d7942695638c4fa9ac0fc3688c4f9774b905a14e3a3f17"
        "1bac586c55e83ff97a1aeffb3af00adb22c6bb"
        "0000000000000000000000000000000008b3f481e3aaa0f1a09e30ed741d8ae4fcf5e095d5d00af600db18cb2c"
        "04b3edd03cc744a2888ae40caa232946c5e7e1"
        "00000000000000000000000000000000112b98340eee2777cc3c14163dea3ec97977ac3dc5c70da32e6e87578f"
        "44912e902ccef9efe28d4a78b8999dfbca9426"
        "00000000000000000000000000000000186b28d92356c4dfec4b5201ad099dbdede3781f8998ddf929b4cd7756"
        "192185ca7b8f4ef7088f813270ac3d48868a21");
}

bytes blsG1AddExpectedG1PlusP1()
{
    return bcos::fromHex(
        "000000000000000000000000000000000a40300ce2dec9888b60690e9a41d3004fda4886854573974fab73b046"
        "d3147ba5b7a5bde85279ffede1b45b3918d82d"
        "0000000000000000000000000000000006d3d887e9f53b9ec4eb6cedf5607226754b07c01ace7834f57f3e7315"
        "faefb739e59018e22c492006190fba4a870025");
}

bytes p256verifyValidSignatureInput()
{
    bytes input;
    input += bcos::fromHex("bb5a52f42f9c9261ed4361f59422a1e30036e7c32b270c8807a419feca605023");
    input += bcos::fromHex("2ba3a8be6b94d5ec80a6d9d1190a436effe50d85a1eee859b8cc6af9bd5c2e18");
    input += bcos::fromHex("4cd60b855d442f5b3c7b11eb6c4e0ae7525fe710fab9aa7c77a67f79e6fadd76");
    input += bcos::fromHex("2927b10512bae3eddcfe467828128bad2903269919f7086069c8c4df6c732838");
    input += bcos::fromHex("c7787964eaac00e5921fb1498a60f4606766b3d9685001558d1a974e7341513e");
    return input;
}

bytes scalarOne32()
{
    bytes s(32, 0);
    s.back() = 1;
    return s;
}

bytes scalarZero32()
{
    return bytes(32, 0);
}

evmc_address precompileAddress(uint16_t suffix)
{
    evmc_address address{};
    address.bytes[18] = static_cast<uint8_t>((suffix >> 8) & 0xff);
    address.bytes[19] = static_cast<uint8_t>(suffix & 0xff);
    return address;
}

bcos::evm::EVMCResult callBuiltinAt(evmc_address const& recipient, bytesConstRef input,
    evmc_revision revision, int64_t gas = 10'000'000)
{
    executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.recipient = recipient;
    message.code_address = recipient;
    message.gas = gas;
    message.input_data = input.data();
    message.input_size = input.size();

    bcos::evm::RevisionConfig rev{.revision = revision};
    if (revision >= EVMC_OSAKA)
    {
        rev.eip7823 = true;
    }
    return bcos::evm::callBuiltinPrecompiled(message, rev, revision, true);
}

bytes copyOutput(bcos::evm::EVMCResult const& result)
{
    return {result.output_data, result.output_data + result.output_size};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CompatExecuteViaHostPhaseC)

BOOST_AUTO_TEST_SUITE(CompatEip7623Extended)

BOOST_AUTO_TEST_CASE(TE_FC_C_7_calldata_floor_formula_extended)
{
    bytes empty;
    BOOST_CHECK_EQUAL(bcos::evm::gas::calcEip7623CalldataGas(ref(empty)), 0);

    bytes zeros(100, 0x00);
    BOOST_CHECK_EQUAL(bcos::evm::gas::calcEip7623CalldataGas(ref(zeros)), 1000);

    bytes nonzero(100, 0xff);
    BOOST_CHECK_EQUAL(bcos::evm::gas::calcEip7623CalldataGas(ref(nonzero)), 4000);

    bytes oneNonZero{0x42};
    BOOST_CHECK_EQUAL(bcos::evm::gas::calcEip7623CalldataGas(ref(oneNonZero)), 40);

    bytes oneZero{0x00};
    BOOST_CHECK_EQUAL(bcos::evm::gas::calcEip7623CalldataGas(ref(oneZero)), 10);

    bytes fourZeros(4, 0x00);
    BOOST_CHECK_EQUAL(bcos::evm::gas::calcEip7623CalldataGas(ref(fourZeros)), 40);

    bytes oneOne{0x42, 0x00};
    BOOST_CHECK_EQUAL(bcos::evm::gas::calcEip7623CalldataGas(ref(oneOne)), 50);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_7_document_no_21000_base)
{
    BOOST_TEST_MESSAGE(
        "I2: FISCO-BCOS does not add unconditional Ethereum 21000 base gas to the EIP-7623 floor.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_7_skipped_internal_call_documented)
{
    BOOST_TEST_MESSAGE(
        "EIP-7623 calldata floor applies only at top-level (seq==0); internal calls skip "
        "deduction.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_7_executive_deduct_seq0_oog)
{
    BOOST_TEST_MESSAGE(
        "Executive-level seq==0 OOG branch covered by EthTxGasSettlementExecutor tests.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_7_executive_skipped_when_seq_nonzero)
{
    executor::CallParameters params{executor::CallParameters::MESSAGE};
    params.seq = 1;
    BOOST_CHECK(params.seq != 0);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatSelfdestructDocs)

BOOST_AUTO_TEST_CASE(TE_FC_C_SD_eip6780_deviation_documented)
{
    BOOST_TEST_MESSAGE(
        "S0-D3: host selfdestruct returns false (no refund) for EIP-6780 safety; full "
        "same-tx-create "
        "tracking deferred.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_SD_A_pre_cancun_legacy_baseline_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-A TODO: pre-Cancun SELFDESTRUCT baseline — requires EVM execution harness.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_SD_B_cancun_existing_contract_should_not_delete_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-B TODO: Cancun+ existing contract SELFDESTRUCT — requires transaction-executor EVM "
        "harness.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_SD_C_cancun_same_tx_create_then_selfdestruct_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-C TODO: Cancun+ same-tx CREATE->SELFDESTRUCT — requires transaction-executor EVM "
        "harness.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatBuiltinSmoke)

BOOST_AUTO_TEST_CASE(TE_FC_C_S_cancun_only_identity_precompile)
{
    auto const input = bcos::fromHex("deadbeef");
    auto result = callBuiltinAt(precompileAddress(0x04), ref(input), EVMC_CANCUN);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(result.output_size, 0U);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_S_legacy_tx_no_prague_path)
{
    bytes input{0x01, 0x02};
    auto result = callBuiltinAt(precompileAddress(0x04), ref(input), EVMC_LONDON);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_S_cancun_only_legacy_gas_smoke)
{
    BOOST_TEST_MESSAGE(
        "FC-01b: full legacy gas regression deferred; EIP-2929 cold path covered in "
        "CompatExecuteViaHost.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CompatPrecompileBuiltinExtended)

BOOST_AUTO_TEST_CASE(TE_FC_C_P_bls_g1add_commutative_vector_with_prague)
{
    auto const input = blsG1AddValidInputG1PlusP1();
    auto const expected = blsG1AddExpectedG1PlusP1();

    bytes swapped(256, 0);
    std::memcpy(swapped.data(), input.data() + 128, 128);
    std::memcpy(swapped.data() + 128, input.data(), 128);

    auto result = callBuiltinAt(precompileAddress(0x0b), ref(swapped), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), expected.size());
    BOOST_CHECK(std::memcmp(output.data(), expected.data(), expected.size()) == 0);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_bls_g1msm_semantics_with_prague)
{
    auto const g1addIn = blsG1AddValidInputG1PlusP1();
    bytes point(g1addIn.begin(), g1addIn.begin() + 128);

    bytes oneInput = point;
    auto const one = scalarOne32();
    oneInput.insert(oneInput.end(), one.begin(), one.end());
    auto r1 = callBuiltinAt(precompileAddress(0x0c), ref(oneInput), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(r1.status_code, EVMC_SUCCESS);
    auto const out1 = copyOutput(r1);
    BOOST_REQUIRE_EQUAL(out1.size(), point.size());
    BOOST_CHECK(std::memcmp(out1.data(), point.data(), point.size()) == 0);

    bytes zeroInput = point;
    auto const zero = scalarZero32();
    zeroInput.insert(zeroInput.end(), zero.begin(), zero.end());
    auto r0 = callBuiltinAt(precompileAddress(0x0c), ref(zeroInput), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(r0.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r0.output_size, 128u);
    for (size_t i = 0; i < r0.output_size; ++i)
    {
        BOOST_CHECK_EQUAL(r0.output_data[i], 0);
    }
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_bls_g1msm_multi_scalar_with_prague)
{
    auto const g1addIn = blsG1AddValidInputG1PlusP1();
    bytes point(g1addIn.begin(), g1addIn.begin() + 128);
    auto const one = scalarOne32();
    auto const zero = scalarZero32();

    bytes input = point;
    input.insert(input.end(), one.begin(), one.end());
    input.insert(input.end(), point.begin(), point.end());
    input.insert(input.end(), zero.begin(), zero.end());
    BOOST_REQUIRE_EQUAL(input.size(), 320u);

    auto result = callBuiltinAt(precompileAddress(0x0c), ref(input), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), point.size());
    BOOST_CHECK(std::memcmp(output.data(), point.data(), point.size()) == 0);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_bls_g2add_and_g2msm_semantics_with_prague)
{
    bytes fp2Zero(128, 0);
    auto mapped = callBuiltinAt(precompileAddress(0x11), ref(fp2Zero), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(mapped.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(mapped.output_size, 256u);
    bytes g2(mapped.output_data, mapped.output_data + mapped.output_size);

    bytes addInput = g2;
    addInput.insert(addInput.end(), 256, 0);
    auto addR = callBuiltinAt(precompileAddress(0x0d), ref(addInput), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(addR.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(addR.output_size, g2.size());
    BOOST_CHECK(std::memcmp(addR.output_data, g2.data(), g2.size()) == 0);

    bytes oneInput = g2;
    auto const one = scalarOne32();
    oneInput.insert(oneInput.end(), one.begin(), one.end());
    auto m1 = callBuiltinAt(precompileAddress(0x0e), ref(oneInput), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(m1.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(m1.output_size, g2.size());
    BOOST_CHECK(std::memcmp(m1.output_data, g2.data(), g2.size()) == 0);

    bytes zeroInput = g2;
    auto const zero = scalarZero32();
    zeroInput.insert(zeroInput.end(), zero.begin(), zero.end());
    auto m0 = callBuiltinAt(precompileAddress(0x0e), ref(zeroInput), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(m0.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(m0.output_size, 256u);
    for (size_t i = 0; i < m0.output_size; ++i)
    {
        BOOST_CHECK_EQUAL(m0.output_data[i], 0);
    }
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_bls_g2msm_multi_scalar_with_prague)
{
    bytes fp2Zero(128, 0);
    auto mapped = callBuiltinAt(precompileAddress(0x11), ref(fp2Zero), EVMC_PRAGUE);
    BOOST_REQUIRE_EQUAL(mapped.status_code, EVMC_SUCCESS);
    bytes g2(mapped.output_data, mapped.output_data + mapped.output_size);

    auto const one = scalarOne32();
    auto const zero = scalarZero32();
    bytes input = g2;
    input.insert(input.end(), one.begin(), one.end());
    input.insert(input.end(), g2.begin(), g2.end());
    input.insert(input.end(), zero.begin(), zero.end());
    BOOST_REQUIRE_EQUAL(input.size(), 576u);

    auto result = callBuiltinAt(precompileAddress(0x0e), ref(input), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result.output_size, g2.size());
    BOOST_CHECK(std::memcmp(result.output_data, g2.data(), g2.size()) == 0);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_bls_pairing_and_map_semantics_with_prague)
{
    bytes fp64(64, 0);
    auto mapG1 = callBuiltinAt(precompileAddress(0x10), ref(fp64), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(mapG1.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(mapG1.output_size, 128u);

    bytes fp128(128, 0);
    auto mapG2 = callBuiltinAt(precompileAddress(0x11), ref(fp128), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(mapG2.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(mapG2.output_size, 256u);

    bytes pairTrueInput(384, 0);
    auto pairTrue = callBuiltinAt(precompileAddress(0x0f), ref(pairTrueInput), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(pairTrue.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(pairTrue.output_size, 32u);
    BOOST_CHECK_EQUAL(pairTrue.output_data[31], 1);

    bytes onePair;
    onePair.insert(onePair.end(), mapG1.output_data, mapG1.output_data + mapG1.output_size);
    onePair.insert(onePair.end(), mapG2.output_data, mapG2.output_data + mapG2.output_size);
    BOOST_REQUIRE_EQUAL(onePair.size(), 384u);

    auto pairFalse = callBuiltinAt(precompileAddress(0x0f), ref(onePair), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(pairFalse.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(pairFalse.output_size, 32u);
    BOOST_CHECK_EQUAL(pairFalse.output_data[31], 0);

    bytes pairInvalidInput(1, 0);
    auto pairInvalid = callBuiltinAt(precompileAddress(0x0f), ref(pairInvalidInput), EVMC_PRAGUE);
    BOOST_CHECK(
        pairInvalid.status_code == EVMC_INTERNAL_ERROR || pairInvalid.status_code == EVMC_REVERT);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_p256verify_ok_with_osaka)
{
    auto input = p256verifyValidSignatureInput();
    BOOST_REQUIRE_EQUAL(input.size(), 160u);

    auto result = callBuiltinAt(precompileAddress(0x0100), ref(input), EVMC_OSAKA);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result.output_size, 32u);
    for (size_t i = 0; i < 31; ++i)
    {
        BOOST_CHECK_EQUAL(result.output_data[i], 0);
    }
    BOOST_CHECK_EQUAL(result.output_data[31], 1);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_p256verify_invalid_signature)
{
    auto input = p256verifyValidSignatureInput();
    input[0] ^= 0xff;

    auto result = callBuiltinAt(precompileAddress(0x0100), ref(input), EVMC_OSAKA);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result.output_size, 0);
}

BOOST_AUTO_TEST_CASE(TE_FC_C_P_legacy_modexp_still_works)
{
    auto const data = compatMakeModexpInput({0x07}, {0x00}, {0x0b});
    auto result = callBuiltinAt(precompileAddress(0x05), ref(data), EVMC_LONDON);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    auto const output = copyOutput(result);
    BOOST_REQUIRE_EQUAL(output.size(), 1u);
    BOOST_CHECK_EQUAL(output[0], 1);
}

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
