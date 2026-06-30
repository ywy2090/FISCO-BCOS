#define BOOST_TEST_MODULE OpStackFeeSettlementCtxTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackChainPolicy.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-evm/opstack/OpStackFeeSettlement.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address fromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(buyGas_failure_records_result_on_ctx_not_fee_context)
{
    state::test::InMemoryStateView stateView;
    auto const sender = fromLastByte(0x43);
    stateView.insert_account(sender, state::Account{.balance = u256(1)});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 50'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    OpStackExecutionRequest input;
    input.gasTipCap = 1;
    input.gasFeeCap = 10;
    input.blockInfo.baseFee = 0;

    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    OpStackFeeSettlement ledger;
    BOOST_REQUIRE(!task::syncWait(ledger.buyGas(view)));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status, protocol::TransactionStatus::NotEnoughCash);
}

BOOST_AUTO_TEST_CASE(buyGas_uses_ctx_message_not_fee_context_copy)
{
    state::test::InMemoryStateView stateView;
    auto const sender = fromLastByte(0x42);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000)});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 50'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    OpStackExecutionRequest input;
    input.gasTipCap = 1;
    input.gasFeeCap = 10;
    input.blockInfo.baseFee = 0;

    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    OpStackFeeSettlement ledger;
    auto ok = task::syncWait(ledger.buyGas(view));
    BOOST_REQUIRE(ok);
    BOOST_CHECK(ctx.state.get_balance(sender) < u256(1'000'000));
}

BOOST_AUTO_TEST_CASE(Settlement_routesCoinbaseBaseFeeL1AndOperator)
{
    state::test::InMemoryStateView stateView;
    auto const sender = fromLastByte(0x01);
    auto const coinbase = fromLastByte(0x02);

    state::Account senderAccount;
    senderAccount.balance = 20'000;
    stateView.insert_account(sender, senderAccount);

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 1'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    OpStackFeeSettlement executor;
    executor.m_l1CostFunc = [](RollupCostData const&, uint64_t) { return u256(100); };
    executor.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 10); };

    OpStackExecutionRequest input;
    input.gasTipCap = 5;
    input.gasFeeCap = 10;
    input.blockInfo.timestamp = 1;
    input.blockInfo.baseFee = 2;
    input.blockInfo.coinbase = coinbase;
    input.rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};

    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    BOOST_REQUIRE(task::syncWait(executor.buyGas(view)));

    OpStackSettlementResult settled;
    settled.gasUsed = 400;
    settled.gasRemaining = 600;

    auto const feePlan = task::syncWait(executor.refundGas(view, settled));
    (void)feePlan;

    BOOST_CHECK_EQUAL(ctx.state.get_balance(coinbase), u256(2'000));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(OP_BASE_FEE_RECIPIENT), u256(800));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(OP_L1_FEE_RECIPIENT), u256(100));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(OP_OPERATOR_FEE_RECIPIENT), u256(410));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), u256(16'690));
}

BOOST_AUTO_TEST_CASE(HardFailure_stillRefundsUnusedGas)
{
    state::test::InMemoryStateView stateView;
    auto const sender = fromLastByte(0x11);
    auto const coinbase = fromLastByte(0x12);

    state::Account senderAccount;
    senderAccount.balance = 10'000;
    stateView.insert_account(sender, senderAccount);

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 500;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    OpStackFeeSettlement executor;
    executor.m_l1CostFunc = [](RollupCostData const&, uint64_t) { return u256(60); };
    executor.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 50); };

    OpStackExecutionRequest input;
    input.gasTipCap = 2;
    input.gasFeeCap = 4;
    input.blockInfo.timestamp = 10;
    input.blockInfo.baseFee = 1;
    input.blockInfo.coinbase = coinbase;
    input.rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};

    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    BOOST_REQUIRE(task::syncWait(executor.buyGas(view)));

    auto const settlement = postExecuteGasSettlement(500, 120, 0, 0);
    OpStackSettlementResult settled;
    settled.gasUsed = static_cast<int64_t>(settlement.gasUsed);
    settled.gasRemaining = settlement.gasRemaining;

    auto const feePlan = task::syncWait(executor.refundGas(view, settled));
    (void)feePlan;

    BOOST_CHECK_EQUAL(ctx.state.get_balance(OP_L1_FEE_RECIPIENT), u256(60));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(OP_OPERATOR_FEE_RECIPIENT), u256(430));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(coinbase), u256(760));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(OP_BASE_FEE_RECIPIENT), u256(380));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), u256(8'370));
}
}  // namespace bcos::evm::test
