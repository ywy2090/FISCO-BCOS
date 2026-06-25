#define BOOST_TEST_MODULE OpStackSettlementTest

#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "bcos-protocol/TransactionStatus.h"
#include "state/InMemoryEvmStateReader.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(finalize_normal_completed_matches_post_execute_settlement)
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_message msg{};
    msg.gas = 100'000;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_SUCCESS;
    raw.gas_left = 80'000;
    raw.gas_refund = 5'000;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
    ctx.exitKind = TxPipelineExitKind::Completed;

    OpStackFeeContext feeCtx;
    feeCtx.m_floorDataGas = 0;

    auto result = finalizeNormal(ctx, feeCtx, ctx.exitKind);

    auto const stateRefund = revision.eip1559 ? 5'000u : 0u;
    auto const expected = postExecuteGasSettlement(100'000u, 80'000u, stateRefund, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
    BOOST_CHECK_EQUAL(result.gasRemaining, expected.gasRemaining);
}

BOOST_AUTO_TEST_CASE(finalize_normal_intrinsic_reject_gas_used_zero)
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_message msg{};
    msg.gas = 50'000;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};
    ctx.exitKind = TxPipelineExitKind::IntrinsicRejected;

    OpStackFeeContext feeCtx;

    auto result = finalizeNormal(ctx, feeCtx, ctx.exitKind);

    BOOST_CHECK_EQUAL(result.gasUsed, int64_t{0});
    BOOST_CHECK_EQUAL(result.gasRemaining, 50'000u);
}

BOOST_AUTO_TEST_CASE(finalize_normal_rules_rejected_applies_settlement)
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_message msg{};
    msg.gas = 100'000;
    auto revision = bcos::evm_standard::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    evmc_result raw{};
    raw.status_code = EVMC_REVERT;
    raw.gas_left = 60'000;
    raw.gas_refund = 0;
    ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::RevertInstruction);
    ctx.exitKind = TxPipelineExitKind::RulesRejected;

    OpStackFeeContext feeCtx;
    feeCtx.m_floorDataGas = 0;

    auto result = finalizeNormal(ctx, feeCtx, ctx.exitKind);

    auto const expected = postExecuteGasSettlement(100'000u, 60'000u, 0u, 0u);
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<int64_t>(expected.gasUsed));
}
}  // namespace bcos::evm::test
