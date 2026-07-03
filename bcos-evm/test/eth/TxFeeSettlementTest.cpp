#define BOOST_TEST_MODULE TxFeeSettlementTest
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip1559.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using bcos::evm::revisionConfigFromRevision;
using bcos::evm::gas::FeeInputs;
using bcos::evm::gas::planPostExecution;
using bcos::evm::gas::planPreExecution;

namespace
{
auto const kLondon = revisionConfigFromRevision(EVMC_LONDON);
auto const kBerlin = revisionConfigFromRevision(EVMC_BERLIN);

FeeInputs makeLondon1559Inputs()
{
    return FeeInputs{
        .revision = kLondon,
        .baseFee = 10,
        .gasLimit = 100'000,
        .gasPrice = 0,
        .gasTipCap = 2,
        .gasFeeCap = 100,
        .web3TypedTxKind = 0x02,
        .hasExplicitFeeCaps = false,
    };
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TxFeeSettlementTest)

BOOST_AUTO_TEST_CASE(pre_execution_type2_london)
{
    auto const inputs = makeLondon1559Inputs();
    auto const plan = planPreExecution(inputs);
    BOOST_CHECK_EQUAL(plan.effectiveGasPrice, 12);
    BOOST_CHECK_EQUAL(plan.maxBalanceDebit, u256(100'000) * 100);
    BOOST_CHECK_EQUAL(plan.preDebitAmount, u256(100'000) * 12);
    BOOST_CHECK_EQUAL(plan.unusedRefund, 0);
}

BOOST_AUTO_TEST_CASE(pre_execution_legacy_berlin)
{
    FeeInputs inputs{
        .revision = kBerlin,
        .baseFee = 0,
        .gasLimit = 21'000,
        .gasPrice = 7,
        .gasTipCap = 0,
        .gasFeeCap = 0,
        .web3TypedTxKind = 0,
        .hasExplicitFeeCaps = false,
    };
    auto const plan = planPreExecution(inputs);
    BOOST_CHECK_EQUAL(plan.effectiveGasPrice, 7);
    BOOST_CHECK_EQUAL(plan.maxBalanceDebit, u256(21'000) * 7);
    BOOST_CHECK_EQUAL(plan.preDebitAmount, u256(21'000) * 7);
}

BOOST_AUTO_TEST_CASE(post_execution_sender_net_equals_tip_plus_base)
{
    auto const inputs = makeLondon1559Inputs();
    auto const gasUsed = 50'000;
    auto const gasRemaining = 50'000;
    auto const plan = planPostExecution(inputs, gasUsed, gasRemaining);
    BOOST_CHECK_EQUAL(plan.unusedRefund, u256(50'000) * 12);
    BOOST_CHECK_EQUAL(plan.coinbaseTip, u256(50'000) * 2);
    BOOST_CHECK_EQUAL(plan.baseFeeAmount, u256(50'000) * 10);
    BOOST_CHECK_EQUAL(plan.senderNetDebit, u256(50'000) * 12);
    BOOST_CHECK_EQUAL(plan.senderNetDebit, plan.coinbaseTip + plan.baseFeeAmount);
}

BOOST_AUTO_TEST_CASE(post_execution_zero_tip_when_effective_equals_base)
{
    FeeInputs inputs{
        .revision = kLondon,
        .baseFee = 10,
        .gasLimit = 21'000,
        .gasPrice = 0,
        .gasTipCap = 0,
        .gasFeeCap = 100,
        .web3TypedTxKind = 0x02,
        .hasExplicitFeeCaps = false,
    };
    auto const plan = planPostExecution(inputs, 21'000, 0);
    BOOST_CHECK_EQUAL(plan.effectiveGasPrice, 10);
    BOOST_CHECK_EQUAL(plan.coinbaseTip, 0);
    BOOST_CHECK_EQUAL(plan.baseFeeAmount, u256(21'000) * 10);
    BOOST_CHECK_EQUAL(plan.senderNetDebit, u256(21'000) * 10);
}

BOOST_AUTO_TEST_CASE(zero_effective_skips_post_amounts)
{
    FeeInputs inputs{
        .revision = kLondon,
        .baseFee = 10,
        .gasLimit = 21'000,
        .gasPrice = 0,
        .gasTipCap = 0,
        .gasFeeCap = 0,
        .web3TypedTxKind = 0x02,
        .hasExplicitFeeCaps = false,
    };
    auto const plan = planPostExecution(inputs, 21'000, 0);
    BOOST_CHECK_EQUAL(plan.effectiveGasPrice, 0);
    BOOST_CHECK_EQUAL(plan.senderNetDebit, 0);
    BOOST_CHECK_EQUAL(plan.coinbaseTip, 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
