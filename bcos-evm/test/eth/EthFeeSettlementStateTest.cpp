#define BOOST_TEST_MODULE EthFeeSettlementStateTest
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/gas/PostExecuteGasMetering.h"
#include "bcos-evm/eth/gas/TopLevelGasSettlement.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/settlement/EthFeeSettlement.h"
#include "bcos-evm/eth/settlement/EthSettlementProjection.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "helpers/InMemoryStateView.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addr(uint8_t b)
{
    evmc_address a{};
    a.bytes[19] = b;
    return a;
}

constexpr bcos::u256 kPreDebit = bcos::u256(21'000) * 100;
}  // namespace

BOOST_AUTO_TEST_CASE(buyGas_debits_sender_on_state)
{
    state::test::InMemoryStateView base;
    auto const initialBalance = kPreDebit + 1'000'000;
    base.insert_account(addr(1), state::Account{.balance = initialBalance});

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 21'000;
    RevisionConfig rev{};
    rev.revision = EVMC_LONDON;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(100));

    EthMessageRequest input{};
    input.blockInfo.baseFee = 10;
    input.gasPrice = 100;
    input.txValue = 0;
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(ok);
    BOOST_CHECK_EQUAL(ctx.state.get_balance(addr(1)), initialBalance - kPreDebit);
    BOOST_CHECK_EQUAL(sidecar.effectiveGasPrice, 100);
}

BOOST_AUTO_TEST_CASE(buyGas_debits_blob_base_fee_on_state)
{
    state::test::InMemoryStateView base;
    constexpr size_t kBlobCount = 6;
    auto const execPreDebit = bcos::u256(100'000) * 7;
    auto const blobDebit = bcos::u256(kBlobCount) * gas::BLOB_GAS_PER_BLOB * 1;
    auto const initialBalance = execPreDebit + blobDebit + 1'000'000;
    base.insert_account(addr(1), state::Account{.balance = initialBalance});

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 100'000;
    RevisionConfig rev{};
    rev.revision = EVMC_CANCUN;
    rev.eip1559 = true;
    rev.eip4844 = true;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(7));

    EthMessageRequest input{};
    input.revisionConfig = rev;
    input.blockInfo.baseFee = 7;
    input.blockInfo.blobBaseFee = 1;
    input.gasTipCap = 0;
    input.gasFeeCap = 7;
    input.blobGasFeeCap = 1;
    input.hasExplicitFeeCaps = true;
    input.web3TypedTxKind = 0x03;
    input.blobVersionedHashes.assign(kBlobCount, h256{0x01});
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(ok);
    BOOST_CHECK_EQUAL(ctx.state.get_balance(addr(1)), initialBalance - execPreDebit - blobDebit);
}

BOOST_AUTO_TEST_CASE(buyGas_insufficient_balance_includes_tx_value_in_afford_check)
{
    state::test::InMemoryStateView base;
    auto const execPreDebit = bcos::u256(100'000) * 7;
    auto const blobDebit = bcos::u256(131'072) * 1;
    auto const txValue = bcos::u256(1);
    auto const initialBalance = execPreDebit + blobDebit + txValue - 1;
    base.insert_account(addr(1), state::Account{.balance = initialBalance});

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 100'000;
    RevisionConfig rev{};
    rev.revision = EVMC_CANCUN;
    rev.eip1559 = true;
    rev.eip4844 = true;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(7));

    EthMessageRequest input{};
    input.revisionConfig = rev;
    input.blockInfo.baseFee = 7;
    input.blockInfo.blobBaseFee = 1;
    input.gasTipCap = 0;
    input.gasFeeCap = 7;
    input.blobGasFeeCap = 1;
    input.hasExplicitFeeCaps = true;
    input.web3TypedTxKind = 0x03;
    input.txValue = 0;
    input.blobVersionedHashes = {h256{0x01}};
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const okWithoutValue = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(okWithoutValue);

    base.insert_account(addr(1), state::Account{.balance = initialBalance});
    StateTransitionContext ctxWithValue(base, msg, rev, bcos::u256(7));
    input.txValue = txValue;
    EthSettlementProjection viewWithValue{ctxWithValue, input, sidecar};
    auto const okWithValue = bcos::task::syncWait(settlement.buyGas(viewWithValue));
    BOOST_REQUIRE(!okWithValue);
}

BOOST_AUTO_TEST_CASE(buyGas_rejects_eest_insufficient_blob_tx_exact_balance_minus_1)
{
    // EEST exact_balance_minus_1: afford gasFeeCap*gasLimit but not + blobGas*maxFeePerBlobGas.
    state::test::InMemoryStateView base;
    auto const gasLimit = int64_t{0x6a40};
    auto const gasFeeCap = bcos::u256{0x0e};
    auto const maxBalanceDebit = bcos::u256(gasLimit) * gasFeeCap;
    auto const blobBalanceCheck = bcos::u256(gas::BLOB_GAS_PER_BLOB) * 1;
    auto const initialBalance = maxBalanceDebit + blobBalanceCheck - 1;
    base.insert_account(addr(1), state::Account{.balance = initialBalance});

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = gasLimit;
    RevisionConfig rev{};
    rev.revision = EVMC_CANCUN;
    rev.eip1559 = true;
    rev.eip4844 = true;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(7));

    EthMessageRequest input{};
    input.revisionConfig = rev;
    input.blockInfo.baseFee = 7;
    input.blockInfo.blobBaseFee = 1;
    input.gasTipCap = 0;
    input.gasFeeCap = gasFeeCap;
    input.blobGasFeeCap = 1;
    input.hasExplicitFeeCaps = true;
    input.web3TypedTxKind = 0x03;
    h256 blobHash{};
    blobHash[0] = 0x01;
    input.blobVersionedHashes = {blobHash};
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(!ok);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(ctx.evmcResult.status, protocol::TransactionStatus::NotEnoughCash);
}

BOOST_AUTO_TEST_CASE(buyGas_insufficient_balance_applies_penalty)
{
    state::test::InMemoryStateView base;
    base.insert_account(addr(1), state::Account{.balance = 500});

    evmc_message msg{};
    msg.sender = addr(1);
    msg.gas = 21'000;
    RevisionConfig rev{};
    rev.revision = EVMC_LONDON;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(100));

    EthMessageRequest input{};
    input.blockInfo.baseFee = 10;
    input.gasPrice = 100;
    input.gasFeeCap = 100;
    input.gasTipCap = 2;
    input.hasExplicitFeeCaps = true;
    input.web3TypedTxKind = 2;
    EthFeeSidecar sidecar;
    EthSettlementProjection view{ctx, input, sidecar};

    EthFeeSettlement settlement;
    auto const ok = bcos::task::syncWait(settlement.buyGas(view));
    BOOST_REQUIRE(!ok);
    // penalty = min(500, TX_BASE_GAS * effective) — balance reduced
    BOOST_CHECK(ctx.state.get_balance(addr(1)) < 500);
    BOOST_CHECK(ctx.evmcResult.status == protocol::TransactionStatus::NotEnoughCash);
}

BOOST_AUTO_TEST_CASE(meterPostExecuteGas_cancun_applies_eip3529_refund)
{
    evmc_message msg{};
    msg.gas = 5'000'000;
    RevisionConfig rev{};
    rev.revision = EVMC_CANCUN;
    rev.eip7623 = false;
    state::test::InMemoryStateView base;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(1));

    ctx.evmcResult.status_code = EVMC_SUCCESS;
    ctx.evmcResult.gas_left = 4'951'681;
    gas::TxGasSettlementContext snap{};
    snap.evmGasRefund = 4'800;

    auto const out =
        gas::meterPostExecuteGas(ctx.originalGasLimit, StateTransitionExitKind::Completed,
            rev.eip7623, rev.calldata_floor_per_token, ctx.evmcResult.gas_left, snap);
    // peakGasUsed=48319; effectiveRefund=min(4800,48319/5)=4800 → gasUsed=43519
    BOOST_CHECK_EQUAL(out.gasUsed, 43'519);
    BOOST_CHECK_EQUAL(out.gasRemaining, 4'956'481u);
}

BOOST_AUTO_TEST_CASE(meterPostExecuteGas_eip7623_uses_settlement_snapshot)
{
    evmc_message msg{};
    msg.gas = 1000;
    RevisionConfig rev{};
    rev.revision = EVMC_PRAGUE;
    rev.eip7623 = true;
    rev.calldata_floor_per_token = 10;
    state::test::InMemoryStateView base;
    StateTransitionContext ctx(base, msg, rev, bcos::u256(1));

    ctx.evmcResult.status_code = EVMC_SUCCESS;
    ctx.evmcResult.gas_left = 800;
    ctx.snapshot.evmGasRefund = 0;
    ctx.snapshot.calldata = {10, 0, 0};  // floor path exercised in real snapshot

    gas::TxGasSettlementContext snap = ctx.snapshot;
    snap.eip7623SnapshotActive = true;  // captureSettlementSnapshot (FloorDataGas mode)
    auto const out =
        gas::meterPostExecuteGas(ctx.originalGasLimit, StateTransitionExitKind::Completed,
            rev.eip7623, rev.calldata_floor_per_token, ctx.evmcResult.gas_left, snap);
    auto const oracleGasUsed = gas::settleTopLevelTransactionGas(
        1000, 800, 0, rev.calldata_floor_per_token, snap.calldata);
    BOOST_CHECK_EQUAL(out.gasUsed, oracleGasUsed);
    BOOST_CHECK_EQUAL(out.gasUsed, 1000);
    BOOST_CHECK_EQUAL(out.gasRemaining, 0);
}
}  // namespace bcos::evm::test
