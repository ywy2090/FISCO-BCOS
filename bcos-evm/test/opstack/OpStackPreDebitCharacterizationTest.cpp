/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStackPreDebitPlan vs pre-refactor buyGas inline oracle.
 * @file OpStackPreDebitCharacterizationTest.cpp
 */
#define BOOST_TEST_MODULE OpStackPreDebitCharacterizationTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/fee/OpStackPreDebitPlan.h"
#include <boost/test/included/unit_test.hpp>
#include <functional>

namespace bcos::evm::test
{
using bcos::evm::gas::FeeInputs;
using bcos::evm::gas::planPreExecution;
using bcos::evm_standard::revisionConfigFromRevision;

namespace
{
auto const kLondon = revisionConfigFromRevision(EVMC_LONDON);

OpStackPreDebitPlan oraclePreRefactorBuyGas(
    OpStackPreDebitInputs const& inputs, OpStackFeeHooks const& hooks) noexcept
{
    OpStackPreDebitPlan oracle;
    oracle.core1559 = planPreExecution(inputs.fee);

    oracle.sidecar.baseFee = inputs.fee.baseFee;
    oracle.sidecar.effectiveGasPrice = oracle.core1559.effectiveGasPrice;

    auto mgval = oracle.core1559.preDebitAmount;

    oracle.sidecar.l1CostCharged = 0;
    if (hooks.l1CostFunc != nullptr && inputs.rollupCostData != nullptr)
    {
        oracle.sidecar.l1CostCharged =
            (*hooks.l1CostFunc)(inputs.rollupCostData->value(), inputs.blockTime);
        mgval += oracle.sidecar.l1CostCharged;
    }

    oracle.sidecar.operatorCostLimit = 0;
    if (hooks.operatorCostFunc != nullptr)
    {
        oracle.sidecar.operatorCostLimit =
            (*hooks.operatorCostFunc)(static_cast<uint64_t>(inputs.fee.gasLimit), inputs.blockTime);
        mgval += oracle.sidecar.operatorCostLimit;
    }

    oracle.blobDebit = 0;
    oracle.blobBalanceCheck = 0;
    if (inputs.blobCount > 0)
    {
        auto const blobGasUsed = bcos::u256(inputs.blobCount) * OP_BLOB_GAS_PER_BLOB;
        oracle.blobDebit = blobGasUsed * inputs.blobBaseFee;
        oracle.blobBalanceCheck = blobGasUsed * inputs.blobGasFeeCap;
        mgval += oracle.blobDebit;
    }

    oracle.totalDebit = mgval;
    oracle.balanceCheck = mgval + inputs.txValue;
    if (inputs.hasGasFeeCap)
    {
        oracle.balanceCheck = oracle.core1559.maxBalanceDebit + oracle.sidecar.l1CostCharged +
                              oracle.sidecar.operatorCostLimit + oracle.blobBalanceCheck +
                              inputs.txValue;
    }

    return oracle;
}

void assertPlansEqual(OpStackPreDebitPlan const& plan, OpStackPreDebitPlan const& oracle)
{
    BOOST_CHECK_EQUAL(plan.core1559.effectiveGasPrice, oracle.core1559.effectiveGasPrice);
    BOOST_CHECK_EQUAL(plan.core1559.maxBalanceDebit, oracle.core1559.maxBalanceDebit);
    BOOST_CHECK_EQUAL(plan.core1559.preDebitAmount, oracle.core1559.preDebitAmount);
    BOOST_CHECK_EQUAL(plan.sidecar.effectiveGasPrice, oracle.sidecar.effectiveGasPrice);
    BOOST_CHECK_EQUAL(plan.sidecar.baseFee, oracle.sidecar.baseFee);
    BOOST_CHECK_EQUAL(plan.sidecar.l1CostCharged, oracle.sidecar.l1CostCharged);
    BOOST_CHECK_EQUAL(plan.sidecar.operatorCostLimit, oracle.sidecar.operatorCostLimit);
    BOOST_CHECK_EQUAL(plan.blobDebit, oracle.blobDebit);
    BOOST_CHECK_EQUAL(plan.blobBalanceCheck, oracle.blobBalanceCheck);
    BOOST_CHECK_EQUAL(plan.totalDebit, oracle.totalDebit);
    BOOST_CHECK_EQUAL(plan.balanceCheck, oracle.balanceCheck);
}

void assertMatchesOracle(OpStackPreDebitInputs const& inputs, OpStackFeeHooks const& hooks)
{
    assertPlansEqual(planOpStackPreDebit(inputs, hooks), oraclePreRefactorBuyGas(inputs, hooks));
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

BOOST_AUTO_TEST_SUITE(OpStackPreDebitCharacterizationTest)

BOOST_AUTO_TEST_CASE(matches_oracle_type2_1559)
{
    OpStackPreDebitInputs inputs{
        .fee = makeType2FeeInputs(),
        .txValue = 1'000,
        .blockTime = 1'700'000'000,
        .hasGasFeeCap = true,
    };
    assertMatchesOracle(inputs, {});
}

BOOST_AUTO_TEST_CASE(matches_oracle_legacy_gas_price)
{
    OpStackPreDebitInputs inputs{
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
        .txValue = 0,
        .hasGasFeeCap = false,
    };
    assertMatchesOracle(inputs, {});
}

BOOST_AUTO_TEST_CASE(matches_oracle_l1_hook)
{
    RollupCostData rollup{.zeroes = 100, .ones = 50, .fastLzSize = 200};
    std::optional<RollupCostData> rollupOpt = rollup;
    std::function<bcos::u256(RollupCostData const&, uint64_t)> l1Func =
        [](RollupCostData const&, uint64_t) { return bcos::u256(42'000); };
    OpStackFeeHooks hooks{.l1CostFunc = &l1Func};

    OpStackPreDebitInputs inputs{
        .fee = makeType2FeeInputs(), .blockTime = 99, .rollupCostData = &rollupOpt};

    assertMatchesOracle(inputs, hooks);
}

BOOST_AUTO_TEST_CASE(matches_oracle_operator_hook)
{
    std::function<bcos::u256(uint64_t, uint64_t)> operatorFunc =
        [](uint64_t gasLimit, uint64_t) { return bcos::u256(gasLimit * 2); };
    OpStackFeeHooks hooks{.operatorCostFunc = &operatorFunc};

    OpStackPreDebitInputs inputs{.fee = makeType2FeeInputs(), .blockTime = 77};
    assertMatchesOracle(inputs, hooks);
}

BOOST_AUTO_TEST_CASE(matches_oracle_blob_tx)
{
    OpStackPreDebitInputs inputs{
        .fee = makeType2FeeInputs(),
        .txValue = 500,
        .hasGasFeeCap = true,
        .blobGasFeeCap = 15,
        .blobBaseFee = 8,
        .blobCount = 2,
    };
    assertMatchesOracle(inputs, {});
}

BOOST_AUTO_TEST_CASE(matches_oracle_null_hooks_with_blob_and_rollup)
{
    RollupCostData rollup{.ones = 1};
    std::optional<RollupCostData> rollupOpt = rollup;
    OpStackPreDebitInputs inputs{
        .fee = makeType2FeeInputs(),
        .txValue = 0,
        .hasGasFeeCap = true,
        .blobGasFeeCap = 3,
        .blobBaseFee = 2,
        .blobCount = 1,
        .rollupCostData = &rollupOpt,
    };
    assertMatchesOracle(inputs, {});
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
