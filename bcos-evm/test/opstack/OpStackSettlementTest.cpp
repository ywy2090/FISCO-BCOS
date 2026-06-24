#define BOOST_TEST_MODULE OpStackSettlementTest

#include "bcos-evm/opstack/OpStackGasSettlement.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"
#include "state/InMemoryEvmStateReader.h"
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

BOOST_AUTO_TEST_CASE(Settlement_routesCoinbaseBaseFeeL1AndOperator)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = fromLastByte(0x01);
    auto const coinbase = fromLastByte(0x02);

    state::Account senderAccount;
    senderAccount.balance = 20'000;
    stateView.insert_account(sender, senderAccount);

    state::State state(stateView);
    OpStackTxFeeLedger executor;
    executor.m_l1CostFunc = [](RollupCostData const&, uint64_t) { return u256(100); };
    executor.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 10); };

    OpStackTxFeeLedger::OpStackTxExecutionData txData;
    txData.m_state = &state;
    txData.m_message.sender = sender;
    txData.m_gasTipCap = 5;
    txData.m_gasFeeCap = 10;
    txData.m_hasGasFeeCap = true;
    txData.m_gasLimit = 1'000;
    txData.m_blockInfo.timestamp = 1;
    txData.m_blockInfo.baseFee = 2;
    txData.m_blockInfo.coinbase = coinbase;
    txData.m_rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};

    BOOST_REQUIRE(task::syncWait(executor.buyGas(txData)));

    txData.m_gasUsed = 400;
    txData.m_gasRemaining = 600;

    task::syncWait(executor.refundGas(txData));

    BOOST_CHECK_EQUAL(state.get_balance(coinbase), u256(2'000));
    BOOST_CHECK_EQUAL(state.get_balance(OP_BASE_FEE_RECIPIENT), u256(800));
    BOOST_CHECK_EQUAL(state.get_balance(OP_L1_FEE_RECIPIENT), u256(100));
    BOOST_CHECK_EQUAL(state.get_balance(OP_OPERATOR_FEE_RECIPIENT), u256(410));
    BOOST_CHECK_EQUAL(state.get_balance(sender), u256(16'690));
}

BOOST_AUTO_TEST_CASE(HardFailure_stillRefundsUnusedGas)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = fromLastByte(0x11);
    auto const coinbase = fromLastByte(0x12);

    state::Account senderAccount;
    senderAccount.balance = 10'000;
    stateView.insert_account(sender, senderAccount);

    state::State state(stateView);
    OpStackTxFeeLedger executor;
    executor.m_l1CostFunc = [](RollupCostData const&, uint64_t) { return u256(60); };
    executor.m_operatorCostFunc = [](uint64_t gas, uint64_t) { return u256(gas + 50); };

    OpStackTxFeeLedger::OpStackTxExecutionData txData;
    txData.m_state = &state;
    txData.m_message.sender = sender;
    txData.m_gasTipCap = 2;
    txData.m_gasFeeCap = 4;
    txData.m_hasGasFeeCap = true;
    txData.m_gasLimit = 500;
    txData.m_blockInfo.timestamp = 10;
    txData.m_blockInfo.baseFee = 1;
    txData.m_blockInfo.coinbase = coinbase;
    txData.m_rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};

    BOOST_REQUIRE(task::syncWait(executor.buyGas(txData)));

    evmc_result failResult{};
    failResult.status_code = EVMC_OUT_OF_GAS;
    failResult.gas_left = 120;
    txData.m_evmcResult.emplace(failResult, protocol::TransactionStatus::OutOfGas);

    auto const settlement = postExecuteGasSettlement(500, 120, 0, 0);
    txData.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);
    txData.m_gasRemaining = settlement.gasRemaining;

    task::syncWait(executor.refundGas(txData));

    BOOST_CHECK_EQUAL(state.get_balance(OP_L1_FEE_RECIPIENT), u256(60));
    BOOST_CHECK_EQUAL(state.get_balance(OP_OPERATOR_FEE_RECIPIENT), u256(430));
    BOOST_CHECK_EQUAL(state.get_balance(coinbase), u256(760));
    BOOST_CHECK_EQUAL(state.get_balance(OP_BASE_FEE_RECIPIENT), u256(380));
    BOOST_CHECK_EQUAL(state.get_balance(sender), u256(8'370));
}
}  // namespace bcos::evm::test
