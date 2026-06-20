/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-SD*: selfdestruct / EIP-6780 documented deviation (S0 D3).
 *
 *  All test cases in this file are documentation stubs. SELFDESTRUCT is an EVM
 *  opcode, not a precompile — the compat harness (CompatHostContextHarness) only
 *  drives precompile calls via compatCallBuiltInPrecompiled.  A contract deploy +
 *  SELFDESTRUCT call requires a full executor stack (BlockContext + state storage
 *  + EVM execution).  These tests will be moved or re-implemented in
 *  transaction-executor/tests/ when the integration harness is available.
 *
 *  @file CompatSelfdestructTest.cpp
 */

#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatSelfdestruct)

BOOST_AUTO_TEST_CASE(FC_SD_eip6780_deviation_documented)
{
    BOOST_TEST_MESSAGE(
        "S0-D3: FISCO allowSelfdestruct=false — SELFDESTRUCT blocked at EthHost hook. "
        "Real harness: transaction-executor/tests/ CompatTransactionExecutorPhaseE "
        "(SD-B/SD-C) + CompatExecuteViaHostPhaseE (SD-C Eth reference). SD-A pre-Cancun "
        "baseline still deferred.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_SD_A_pre_cancun_legacy_baseline_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-A TODO: <Cancun baseline. Requires contract deploy + SELFDESTRUCT via EVM "
        "execution (not a precompile call). Skipped in compat harness — move to "
        "transaction-executor/tests/ when available.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_SD_B_cancun_existing_contract_should_not_delete_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-B (legacy stub): real harness at TE_FC_E_SD_existing_contract_keeps_code in "
        "transaction-executor/tests/CompatTransactionExecutorPhaseETest.cpp — pre-existing "
        "contract CALL SELFDESTRUCT retains code on FISCO (allowSelfdestruct=false).");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_SD_C_cancun_same_tx_create_then_selfdestruct_todo)
{
    BOOST_TEST_MESSAGE(
        "SD-C TODO (legacy stub): real harness lives in transaction-executor/tests/ — "
        "TE_FC_E_SD_same_tx_create_destroy_fisco (FISCO retains) and "
        "TE_FC_E_SD_same_tx_create_destroy_eth_reference (Eth destroys). "
        "Pair with SD-B TE_FC_E_SD_existing_contract_keeps_code for EIP-6780 matrix.");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatSelfdestruct
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
