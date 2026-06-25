#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "helpers/InMemoryEvmStateReader.h"
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
    state::test::InMemoryEvmStateReader stateView;
    evmc_address sender{};
    evmc_address coinbase{};
    evmc_message message{};
    TxPipelineContext ctx;
    OpStackFeeContext feeCtx;
    OpStackTxFeeLedger ledger;
    GasPoolSpy spy;

    NormalSettleFixture(int64_t gasLimit, TxPipelineExitKind exitKind, int64_t gasLeft,
        evmc_status_code status = EVMC_SUCCESS, int64_t gasRefund = 0)
      : sender(addressFromLastByte(0x01)),
        coinbase(addressFromLastByte(0x02)),
        message(makeMessage(sender, gasLimit)),
        ctx(stateView, message, bcos::evm_standard::makeIsthmusRevisionConfig(), bcos::u256(0))
    {
        stateView.insert_account(sender, state::Account{.balance = u256(2'000'000)});

        evmc_result raw{};
        raw.status_code = status;
        raw.gas_left = gasLeft;
        raw.gas_refund = gasRefund;
        ctx.evmcResult = EVMCResult(raw, protocol::TransactionStatus::None);
        ctx.exitKind = exitKind;

        feeCtx.m_gasTipCap = 5;
        feeCtx.m_gasFeeCap = 10;
        feeCtx.m_hasGasFeeCap = true;
        feeCtx.m_blockInfo.timestamp = 1;
        feeCtx.m_blockInfo.baseFee = 2;
        feeCtx.m_blockInfo.coinbase = coinbase;
        feeCtx.m_rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};

        ledger.m_l1CostFunc = [](RollupCostData const&, uint64_t) { return u256(100); };
        ledger.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 10); };
    }

    void buyGas()
    {
        auto ok = task::syncWait(ledger.buyGas(ctx, feeCtx));
        BOOST_REQUIRE(ok);
    }
};

inline void assertGasPoolMatchesSettled(
    GasPoolSpy const& spy, OpStackSettlementResult const& settled)
{
    BOOST_REQUIRE_EQUAL(spy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(spy.lastRemaining, settled.gasRemaining);
    BOOST_CHECK_EQUAL(spy.lastUsed, static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed)));
}

inline void assertSettleNormalMatchesFinalizeOracle(
    NormalSettleFixture const& fixture, OpStackSettlementResult const& settled)
{
    auto const oracle = finalizeNormal(fixture.ctx, fixture.feeCtx, fixture.ctx.exitKind);
    BOOST_CHECK_EQUAL(settled.gasUsed, oracle.gasUsed);
    BOOST_CHECK_EQUAL(settled.gasRemaining, oracle.gasRemaining);
    BOOST_CHECK_EQUAL(settled.maxUsedGas, oracle.maxUsedGas);
}

struct DepositSettleFixture
{
    state::test::InMemoryEvmStateReader stateView;
    evmc_address sender{};
    evmc_message message{};
    TxPipelineContext ctx;
    GasPoolSpy spy;

    DepositSettleFixture(
        int64_t gasLimit, TxPipelineExitKind exitKind, evmc_status_code evmStatus, int64_t gasLeft)
      : sender(addressFromLastByte(0x61)),
        message(makeMessage(sender, gasLimit)),
        ctx(stateView, message, bcos::evm_standard::makeIsthmusRevisionConfig(), bcos::u256(0))
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
