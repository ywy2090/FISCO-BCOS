#define BOOST_TEST_MODULE BlobGasBalanceTest

#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/opstack/OpStackPreCheck.h"
#include "bcos-evm/opstack/OpStackTxExecutor.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}
}  // namespace

BOOST_AUTO_TEST_CASE(blob_gas_fee_cap_under_blob_base_fee_is_rejected)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x91);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);

    OpStackExecuteViaHostInput input;
    input.message.kind = EVMC_CALL;
    input.message.gas = 100'000;
    input.message.sender = sender;
    input.message.recipient = addressFromLastByte(0x92);
    input.message.code_address = input.message.recipient;
    input.nonce = 0;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.revisionConfig.eip4844 = true;
    input.blockInfo.baseFee = 1;
    input.blockInfo.blobBaseFee = 100;
    input.blobVersionedHashes.push_back(h256(1));
    input.blobGasFeeCap = 99;

    auto error = opStackPreCheck(input, state);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(buy_gas_deducts_blob_base_fee_times_blob_gas)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x93);
    auto const initialBalance = u256(3'000'000);
    stateView.insert_account(sender, state::Account{.balance = initialBalance, .nonce = 0});
    state::State state(stateView);

    OpStackTxExecutor executor;
    OpStackTxExecutor::OpStackTxExecutionData txData;
    txData.m_state = &state;
    txData.m_message.sender = sender;
    txData.m_gasTipCap = 1;
    txData.m_gasFeeCap = 2;
    txData.m_hasGasFeeCap = true;
    txData.m_gasLimit = 1'000;
    txData.m_blockInfo.baseFee = 1;
    txData.m_blockInfo.blobBaseFee = 10;
    txData.m_blobGasFeeCap = 20;
    txData.m_blobVersionedHashes.push_back(h256(1));

    auto const executionGasCost = u256(1'000) * u256(2);
    auto const blobGasCost = u256(OP_BLOB_GAS_PER_BLOB) * u256(10);
    auto const expectedDeduction = executionGasCost + blobGasCost;

    BOOST_REQUIRE(task::syncWait(executor.buyGas(txData)));
    BOOST_CHECK_EQUAL(state.get_balance(sender), initialBalance - expectedDeduction);
}

BOOST_AUTO_TEST_CASE(buy_gas_rejects_insufficient_balance_for_blob_cost)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x94);
    stateView.insert_account(sender, state::Account{.balance = u256(1'500'000), .nonce = 0});
    state::State state(stateView);

    OpStackTxExecutor executor;
    OpStackTxExecutor::OpStackTxExecutionData txData;
    txData.m_state = &state;
    txData.m_message.sender = sender;
    txData.m_gasTipCap = 1;
    txData.m_gasFeeCap = 2;
    txData.m_hasGasFeeCap = true;
    txData.m_gasLimit = 1'000;
    txData.m_blockInfo.baseFee = 1;
    txData.m_blockInfo.blobBaseFee = 10;
    txData.m_blobGasFeeCap = 20;
    txData.m_blobVersionedHashes.push_back(h256(1));

    auto const balanceBefore = state.get_balance(sender);
    BOOST_REQUIRE(!task::syncWait(executor.buyGas(txData)));
    BOOST_REQUIRE(txData.m_evmcResult.has_value());
    BOOST_CHECK_EQUAL(txData.m_evmcResult->status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(state.get_balance(sender), balanceBefore);
}
}  // namespace bcos::evm::test
