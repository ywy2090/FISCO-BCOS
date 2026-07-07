/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStackPostSettlementPlan vs pre-refactor refundGas oracle.
 * @file OpStackPostSettlementCharacterizationTest.cpp
 */
#define BOOST_TEST_MODULE OpStackPostSettlementCharacterizationTest

#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/opstack/fee/OpStackPostSettlementPlan.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <boost/test/included/unit_test.hpp>
#include <functional>

namespace bcos::evm::test
{
using bcos::evm::revisionConfigFromRevision;
using bcos::evm::gas::FeeInputs;
using bcos::evm::gas::planPostExecution;
using bcos::evm::gas::planPreExecution;

namespace
{
auto const kLondon = revisionConfigFromRevision(EVMC_LONDON);

OpStackPostSettlementPlan oraclePreRefactorRefundGas(
    OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks) noexcept
{
    OpStackPostSettlementPlan oracle;
    oracle.core1559 = planPostExecution(inputs.fee, inputs.gasUsed, inputs.gasRemaining);
    oracle.l1FeeRouted = inputs.l1CostCharged;
    if (hooks.operatorCostFunc != nullptr)
    {
        auto const used = static_cast<uint64_t>(std::max<int64_t>(0, inputs.gasUsed));
        oracle.operatorFeeCharged = (*hooks.operatorCostFunc)(used, inputs.blockTime);
        if (oracle.operatorFeeCharged < inputs.operatorCostLimit)
        {
            oracle.senderOperatorRefund = inputs.operatorCostLimit - oracle.operatorFeeCharged;
        }
    }
    return oracle;
}

void assertPlansEqual(
    OpStackPostSettlementPlan const& plan, OpStackPostSettlementPlan const& oracle)
{
    BOOST_CHECK_EQUAL(plan.core1559.unusedRefund, oracle.core1559.unusedRefund);
    BOOST_CHECK_EQUAL(plan.core1559.coinbaseTip, oracle.core1559.coinbaseTip);
    BOOST_CHECK_EQUAL(plan.core1559.baseFeeAmount, oracle.core1559.baseFeeAmount);
    BOOST_CHECK_EQUAL(plan.l1FeeRouted, oracle.l1FeeRouted);
    BOOST_CHECK_EQUAL(plan.operatorFeeCharged, oracle.operatorFeeCharged);
    BOOST_CHECK_EQUAL(plan.senderOperatorRefund, oracle.senderOperatorRefund);
}

void assertMatchesOracle(OpStackPostSettlementInputs const& inputs, OpStackFeeHooks const& hooks)
{
    assertPlansEqual(
        planOpStackPostSettlement(inputs, hooks), oraclePreRefactorRefundGas(inputs, hooks));
}

FeeInputs makeType2FeeInputs()
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

BOOST_AUTO_TEST_SUITE(OpStackPostSettlementCharacterizationTest)

BOOST_AUTO_TEST_CASE(matches_oracle_type2_1559)
{
    OpStackPostSettlementInputs inputs{
        .fee = makeType2FeeInputs(),
        .gasUsed = 120'000,
        .gasRemaining = 380'000,
        .blockTime = 1'700'000'000,
    };
    assertMatchesOracle(inputs, {});
}

BOOST_AUTO_TEST_CASE(matches_oracle_legacy_gas_price)
{
    OpStackPostSettlementInputs inputs{
        .fee =
            FeeInputs{
                .revision = kLondon,
                .baseFee = 10,
                .gasLimit = 100'000,
                .gasPrice = 25,
                .gasTipCap = 0,
                .gasFeeCap = 0,
                .web3TypedTxKind = 0,
                .hasExplicitFeeCaps = false,
            },
        .gasUsed = 30'000,
        .gasRemaining = 70'000,
    };
    assertMatchesOracle(inputs, {});
}

BOOST_AUTO_TEST_CASE(matches_oracle_l1_passthrough)
{
    RollupCostData rollup{.zeroes = 100, .ones = 50, .fastLzSize = 200};
    std::function<bcos::u256(RollupCostData const&, uint64_t)> l1Func =
        [](RollupCostData const&, uint64_t) { return bcos::u256(999'999); };
    OpStackFeeHooks hooks{.l1CostFunc = &l1Func};

    OpStackPostSettlementInputs inputs{
        .fee = makeType2FeeInputs(),
        .gasUsed = 50'000,
        .gasRemaining = 450'000,
        .blockTime = 99,
        .l1CostCharged = 42'000,
    };
    auto const plan = planOpStackPostSettlement(inputs, hooks);
    assertMatchesOracle(inputs, hooks);
    BOOST_CHECK_EQUAL(plan.l1FeeRouted, bcos::u256(42'000));
}

BOOST_AUTO_TEST_CASE(matches_oracle_operator_hook)
{
    std::function<bcos::u256(uint64_t, uint64_t)> operatorFunc = [](uint64_t gasUsed, uint64_t) {
        return bcos::u256(gasUsed * 2);
    };
    OpStackFeeHooks hooks{.operatorCostFunc = &operatorFunc};

    OpStackPostSettlementInputs inputs{
        .fee = makeType2FeeInputs(),
        .gasUsed = 80'000,
        .gasRemaining = 420'000,
        .blockTime = 77,
        .operatorCostLimit = 200'000,
    };
    auto const plan = planOpStackPostSettlement(inputs, hooks);
    assertMatchesOracle(inputs, hooks);
    BOOST_CHECK_EQUAL(plan.operatorFeeCharged, bcos::u256(160'000));
    BOOST_CHECK_EQUAL(plan.senderOperatorRefund, bcos::u256(40'000));
}

BOOST_AUTO_TEST_CASE(matches_oracle_null_hooks)
{
    OpStackPostSettlementInputs inputs{
        .fee = makeType2FeeInputs(),
        .gasUsed = 10'000,
        .gasRemaining = 490'000,
        .l1CostCharged = 12'345,
        .operatorCostLimit = 50'000,
    };
    auto const plan = planOpStackPostSettlement(inputs, {});
    assertMatchesOracle(inputs, {});
    BOOST_CHECK_EQUAL(plan.l1FeeRouted, bcos::u256(12'345));
    BOOST_CHECK_EQUAL(plan.operatorFeeCharged, bcos::u256(0));
    BOOST_CHECK_EQUAL(plan.senderOperatorRefund, bcos::u256(0));
}

BOOST_AUTO_TEST_CASE(post_effective_gas_price_matches_sidecar_snapshot)
{
    auto const fee = makeType2FeeInputs();
    OpStackPostSettlementInputs inputs{
        .fee = fee,
        .gasUsed = 120'000,
        .gasRemaining = 380'000,
        .l1CostCharged = 42'000,
        .operatorCostLimit = 1'000'000,
    };
    auto const plan = planOpStackPostSettlement(inputs, {});
    BOOST_CHECK_EQUAL(plan.core1559.effectiveGasPrice, planPreExecution(fee).effectiveGasPrice);
}

BOOST_AUTO_TEST_CASE(isthmus_refund_limit_minus_used)
{
    std::function<bcos::u256(uint64_t, uint64_t)> op = [](uint64_t gas, uint64_t) {
        return bcos::u256(gas + 1000);
    };
    OpStackFeeHooks hooks{.operatorCostFunc = &op};
    OpStackPostSettlementInputs inputs{
        .fee = FeeInputs{.revision = kLondon, .gasLimit = 1'618},
        .gasUsed = 500,
        .gasRemaining = 1'118,
        .blockTime = 1,
        .operatorCostLimit = 2'618,
    };
    auto const plan = planOpStackPostSettlement(inputs, hooks);
    assertMatchesOracle(inputs, hooks);
    BOOST_CHECK_EQUAL(plan.operatorFeeCharged, bcos::u256(1'500));
    BOOST_CHECK_EQUAL(plan.senderOperatorRefund, bcos::u256(1'118));
}

BOOST_AUTO_TEST_CASE(operator_at_limit_no_sender_refund)
{
    std::function<bcos::u256(uint64_t, uint64_t)> op = [](uint64_t gas, uint64_t) {
        return bcos::u256(gas * 2);
    };
    OpStackFeeHooks hooks{.operatorCostFunc = &op};
    OpStackPostSettlementInputs inputs{
        .fee = makeType2FeeInputs(),
        .gasUsed = 100'000,
        .gasRemaining = 400'000,
        .blockTime = 1,
        .operatorCostLimit = 200'000,
    };
    auto const plan = planOpStackPostSettlement(inputs, hooks);
    assertMatchesOracle(inputs, hooks);
    BOOST_CHECK(plan.operatorFeeCharged >= inputs.operatorCostLimit);
    BOOST_CHECK_EQUAL(plan.senderOperatorRefund, bcos::u256(0));
}

BOOST_AUTO_TEST_CASE(l1_hook_not_invoked_on_post)
{
    std::function<bcos::u256(RollupCostData const&, uint64_t)> l1Func = [](RollupCostData const&,
                                                                            uint64_t) {
        BOOST_FAIL("l1CostFunc must not be called during post-settlement planning");
        return bcos::u256(0);
    };
    OpStackFeeHooks hooks{.l1CostFunc = &l1Func};
    OpStackPostSettlementInputs inputs{
        .fee = makeType2FeeInputs(),
        .gasUsed = 50'000,
        .gasRemaining = 450'000,
        .l1CostCharged = 42'000,
    };
    auto const plan = planOpStackPostSettlement(inputs, hooks);
    BOOST_CHECK_EQUAL(plan.l1FeeRouted, bcos::u256(42'000));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
