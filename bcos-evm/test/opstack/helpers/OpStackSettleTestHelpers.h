#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include "bcos-evm/opstack/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackFeeSettlement.h"
#include "bcos-evm/opstack/OpStackIsthmusRevision.h"
#include "bcos-evm/opstack/OpStackNormalTxFeeCoordinator.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>

namespace bcos::evm::test
{
struct GasPoolSpy
{
    int returnGasCallCount{0};
    uint64_t lastRemaining{0};
    uint64_t lastUsed{0};

    GasPoolHooks hooks()
    {
        GasPoolHooks out{};
        out.returnGas = [this](uint64_t gasRemaining, uint64_t gasUsed) {
            ++returnGasCallCount;
            lastRemaining = gasRemaining;
            lastUsed = gasUsed;
        };
        return out;
    }
};

inline evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

inline evmc_message makeMessage(evmc_address const& sender, int64_t gasLimit)
{
    evmc_message msg{};
    msg.sender = sender;
    msg.gas = gasLimit;
    return msg;
}

struct NormalSettleFixture
{
    state::test::InMemoryStateView stateView;
    evmc_address sender{};
    evmc_address coinbase{};
    evmc_message message{};
    StateTransitionContext ctx;
    OpStackExecutionRequest input;
    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view;
    OpStackFeeSettlement ledger;
    OpStackNormalTxFeeCoordinator settlement;
    OpStackFeeParams feeParams{};
    GasPoolSpy spy;

    NormalSettleFixture(int64_t gasLimit, StateTransitionExitKind exitKind, int64_t gasLeft,
        evmc_status_code status = EVMC_SUCCESS, int64_t gasRefund = 0)
      : sender(addressFromLastByte(0x01)),
        coinbase(addressFromLastByte(0x02)),
        message(makeMessage(sender, gasLimit)),
        ctx(stateView, message, bcos::evm::makeIsthmusRevisionConfig(), bcos::u256(0)),
        view(ctx, input, sidecar),
        settlement(ledger)
    {
        stateView.insert_account(sender, state::Account{.balance = u256(2'000'000)});

        evmc_result raw{};
        raw.status_code = status;
        raw.gas_left = gasLeft;
        raw.gas_refund = gasRefund;
        ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
        ctx.exitKind = exitKind;

        input.gasTipCap = 5;
        input.gasFeeCap = 10;
        input.blockInfo.timestamp = 1;
        input.blockInfo.baseFee = 2;
        input.blockInfo.coinbase = coinbase;
        input.rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};

        ledger.m_l1CostFunc = [](RollupCostData const&, uint64_t) { return u256(100); };
        ledger.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 10); };
    }

    void checkpointBeforeBuyGas() { ctx.state.checkpoint(); }

    bool buyGas()
    {
        OpStackExecutionResult output;
        auto const ok = task::syncWait(settlement.buyGas(view, spy.hooks(), output));
        return ok;
    }

    OpStackExecutionResult completeAfterPipeline()
    {
        OpStackExecutionResult output;
        task::syncWait(settlement.completeAfterPipeline(view, feeParams, spy.hooks(), output));
        return output;
    }

    void prepareAndComplete()
    {
        checkpointBeforeBuyGas();
        BOOST_REQUIRE(buyGas());
    }
};

inline void assertGasPoolMatchesSettled(
    GasPoolSpy const& spy, OpStackSettlementResult const& settled)
{
    BOOST_REQUIRE_EQUAL(spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(spy.lastRemaining, settled.gasRemaining);
    BOOST_CHECK_EQUAL(spy.lastUsed, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
}

inline void assertCompleteOutputMatchesFinalizeOracle(
    NormalSettleFixture const& fixture, OpStackExecutionResult const& output)
{
    auto const oracle = finalizeNormal(fixture.ctx, fixture.sidecar, fixture.ctx.exitKind);
    BOOST_CHECK_EQUAL(output.gasUsed, oracle.gasUsed);
    assertGasPoolMatchesSettled(fixture.spy, oracle);
}

struct DepositSettleFixture
{
    state::test::InMemoryStateView stateView;
    evmc_address sender{};
    evmc_message message{};
    StateTransitionContext ctx;
    GasPoolSpy spy;

    DepositSettleFixture(int64_t gasLimit, StateTransitionExitKind exitKind,
        evmc_status_code evmStatus, int64_t gasLeft)
      : sender(addressFromLastByte(0x61)),
        message(makeMessage(sender, gasLimit)),
        ctx(stateView, message, bcos::evm::makeIsthmusRevisionConfig(), bcos::u256(0))
    {
        stateView.insert_account(sender, state::Account{.balance = 0, .nonce = 5});

        ctx.state.checkpoint();
        ctx.state.set_balance(sender, bcos::u256(999));

        evmc_result raw{};
        raw.status_code = evmStatus;
        raw.gas_left = gasLeft;
        raw.gas_refund = 0;
        ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
        ctx.exitKind = exitKind;
    }
};
}  // namespace bcos::evm::test
