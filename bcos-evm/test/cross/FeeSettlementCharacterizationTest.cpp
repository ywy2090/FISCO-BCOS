/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief characterization: FeeSettlementPlan vs legacy path oracles.
 * @file FeeSettlementCharacterizationTest.cpp
 */
#define BOOST_TEST_MODULE FeeSettlementCharacterizationTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip1559.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using bcos::evm::gas::FeeInputs;
using bcos::evm::gas::FeeSettlementPlan;
using bcos::evm::gas::planPostExecution;
using bcos::evm::gas::planPreExecution;
using bcos::evm_standard::revisionConfigFromRevision;

namespace
{
auto const kLondon = revisionConfigFromRevision(EVMC_LONDON);

/// Oracle: EthTxFeeSettlement buyGas cap path (pre-refactor inline math).
FeeSettlementPlan oracleEthTePre(FeeInputs const& inputs)
{
    auto const caps = gas::normalizeGasCaps(inputs.gasPrice, inputs.gasTipCap, inputs.gasFeeCap,
        inputs.web3TypedTxKind, inputs.hasExplicitFeeCaps, inputs.revision);
    FeeSettlementPlan oracle{};
    oracle.effectiveGasPrice =
        gas::resolveEffectiveGasPrice(caps.gasTipCap, caps.gasFeeCap, inputs.baseFee);
    oracle.maxBalanceDebit = gas::maxBalanceGasDebit(inputs.gasLimit, caps);
    if (inputs.gasLimit > 0 && oracle.effectiveGasPrice > 0)
    {
        oracle.preDebitAmount = u256(inputs.gasLimit) * oracle.effectiveGasPrice;
    }
    return oracle;
}

/// Oracle: EthTxFeeSettlement refundGas + applyGstTransactionSettlement post amounts.
FeeSettlementPlan oracleEthTePost(FeeInputs const& inputs, int64_t gasUsed, int64_t gasRemaining)
{
    auto const pre = oracleEthTePre(inputs);
    FeeSettlementPlan oracle = pre;
    auto const used = std::max<int64_t>(0, gasUsed);
    auto const remaining = std::max<int64_t>(0, gasRemaining);
    if (pre.effectiveGasPrice == 0)
    {
        return oracle;
    }
    if (remaining > 0)
    {
        oracle.unusedRefund = u256(remaining) * pre.effectiveGasPrice;
    }
    if (used > 0)
    {
        oracle.coinbaseTip = u256(used) * gas::tipPerGas(pre.effectiveGasPrice, inputs.baseFee);
        oracle.baseFeeAmount = u256(used) * inputs.baseFee;
        oracle.senderNetDebit = u256(used) * pre.effectiveGasPrice;
    }
    return oracle;
}

/// Oracle: OpStackFeeSettlement refundGas 1559 portion (before sidecar routing).
FeeSettlementPlan oracleOpStackPost1559(
    FeeInputs const& inputs, int64_t gasUsed, int64_t gasRemaining)
{
    auto const pre = oracleEthTePre(inputs);
    FeeSettlementPlan oracle = pre;
    auto const used = static_cast<uint64_t>(std::max<int64_t>(0, gasUsed));
    auto const remaining = static_cast<uint64_t>(std::max<int64_t>(0, gasRemaining));
    if (pre.effectiveGasPrice == 0)
    {
        return oracle;
    }
    oracle.unusedRefund = u256(remaining) * pre.effectiveGasPrice;
    auto const effectiveTip = (pre.effectiveGasPrice > inputs.baseFee) ?
                                  (pre.effectiveGasPrice - inputs.baseFee) :
                                  u256(0);
    oracle.coinbaseTip = u256(used) * effectiveTip;
    oracle.baseFeeAmount = u256(used) * inputs.baseFee;
    oracle.senderNetDebit = u256(used) * pre.effectiveGasPrice;
    return oracle;
}

void assertPrePlansEqual(FeeSettlementPlan const& plan, FeeSettlementPlan const& oracle)
{
    BOOST_CHECK_EQUAL(plan.effectiveGasPrice, oracle.effectiveGasPrice);
    BOOST_CHECK_EQUAL(plan.maxBalanceDebit, oracle.maxBalanceDebit);
    BOOST_CHECK_EQUAL(plan.preDebitAmount, oracle.preDebitAmount);
}

void assertPostPlansEqual(FeeSettlementPlan const& plan, FeeSettlementPlan const& oracle)
{
    assertPrePlansEqual(plan, oracle);
    BOOST_CHECK_EQUAL(plan.unusedRefund, oracle.unusedRefund);
    BOOST_CHECK_EQUAL(plan.coinbaseTip, oracle.coinbaseTip);
    BOOST_CHECK_EQUAL(plan.baseFeeAmount, oracle.baseFeeAmount);
    BOOST_CHECK_EQUAL(plan.senderNetDebit, oracle.senderNetDebit);
}

FeeInputs makeCaseA()
{
    return FeeInputs{
        .revision = kLondon,
        .baseFee = 10,
        .gasLimit = 500'000,
        .gasPrice = 0,
        .gasTipCap = 3,
        .gasFeeCap = 200,
        .web3TypedTxKind = 0x02,
        .hasExplicitFeeCaps = false,
    };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FeeSettlementCharacterizationTest)

BOOST_AUTO_TEST_CASE(pre_matches_eth_te_oracle_type2)
{
    auto const inputs = makeCaseA();
    assertPrePlansEqual(planPreExecution(inputs), oracleEthTePre(inputs));
}

BOOST_AUTO_TEST_CASE(pre_matches_eth_te_oracle_legacy_gas_price)
{
    FeeInputs inputs{
        .revision = kLondon,
        .baseFee = 10,
        .gasLimit = 100'000,
        .gasPrice = 25,
        .gasTipCap = 0,
        .gasFeeCap = 0,
        .web3TypedTxKind = 0,
        .hasExplicitFeeCaps = false,
    };
    assertPrePlansEqual(planPreExecution(inputs), oracleEthTePre(inputs));
}

BOOST_AUTO_TEST_CASE(post_matches_eth_te_and_gst_oracle)
{
    auto const inputs = makeCaseA();
    auto const gasUsed = 120'000;
    auto const gasRemaining = 380'000;
    auto const plan = planPostExecution(inputs, gasUsed, gasRemaining);
    assertPostPlansEqual(plan, oracleEthTePost(inputs, gasUsed, gasRemaining));
}

BOOST_AUTO_TEST_CASE(post_matches_opstack_1559_oracle)
{
    auto const inputs = makeCaseA();
    auto const gasUsed = 120'000;
    auto const gasRemaining = 380'000;
    auto const plan = planPostExecution(inputs, gasUsed, gasRemaining);
    assertPostPlansEqual(plan, oracleOpStackPost1559(inputs, gasUsed, gasRemaining));
}

BOOST_AUTO_TEST_CASE(opstack_hand_rolled_tip_equals_tip_per_gas)
{
    auto const inputs = makeCaseA();
    auto const plan = planPostExecution(inputs, 50'000, 450'000);
    auto const tipViaHandRoll = (plan.effectiveGasPrice > inputs.baseFee) ?
                                    (plan.effectiveGasPrice - inputs.baseFee) :
                                    u256(0);
    BOOST_CHECK_EQUAL(plan.coinbaseTip, u256(50'000) * tipViaHandRoll);
    BOOST_CHECK_EQUAL(
        plan.coinbaseTip, u256(50'000) * gas::tipPerGas(plan.effectiveGasPrice, inputs.baseFee));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
