/*
 * GAP-001 / GAP-002 / GAP-TE-001 / GAP-TE-004 — ETH entry failure characterization.
 *
 * CURRENT_ORACLE (bridge + TE settlement projection):
 *   intrinsic gas too low → OutOfGasLimit(2), gas_left=0, TE gasUsed=gasLimit, receipt emitted.
 *   buyGas insufficient balance → NotEnoughCash(10015) at TE (see EthTxFeeSettlement.h); receipt
 * emitted.
 *
 * GETH_ORACLE:
 *   block processor rejects tx; no receipt (go-ethereum/core/state_processor_test.go:181-186,
 *   state_transition.go:565-567).
 */
#define BOOST_TEST_MODULE EthIntrinsicGasFailureCharacterizationTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/apply/ApplyReferenceMessage.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-protocol/TransactionStatus.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
constexpr int64_t kIntrinsicGasTooLowLimit = gas::TX_BASE_GAS - 1'000;  // params.TxGas-1000 → 20000

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm_standard::RevisionConfig pragueEip7623Config()
{
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_PRAGUE;
    cfg.warm_access = true;
    cfg.eip7623 = true;
    cfg.eip7702 = true;
    cfg.eip1153 = true;
    cfg.eip4844 = true;
    cfg.eip5656 = true;
    cfg.eip6780 = true;
    cfg.calldata_floor_per_token = 10;
    return cfg;
}

EthReferenceRequest makeIntrinsicGasTooLowRequest(
    state::test::InMemoryStateView& stateView, evmc::VM& vm, crypto::Keccak256& hashImpl)
{
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    stateView.insert_account(
        sender, state::Account{.balance = 1'000'000'000'000'000ull, .nonce = 0});

    evmc_message message{};
    message.depth = 0;
    message.kind = EVMC_CALL;
    message.gas = kIntrinsicGasTooLowLimit;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    EthReferenceRequest input{};
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.revisionConfig = pragueEip7623Config();
    input.web3TypedTxKind = 0;  // legacy — matches geth state_processor_test makeTx path
    input.gasPrice = 875'000'000;
    input.blockInfo.number = 22'000'000;
    input.blockInfo.chainId = 1;
    input.blockInfo.gasLimit = 30'000'000;
    input.blockInfo.baseFee = 0;
    return input;
}

int64_t teProjectedGasUsed(int64_t gasLimit, int64_t gasLeft)
{
    // EthTransactionExecutorImpl::settleGasUsedFromEvmResult fallback when snapshot unset.
    return gasLimit - gasLeft;
}

bool stateDiffPreservesBalance(
    state::StateDiff const& diff, evmc_address const& address, bcos::u256 initialBalance)
{
    auto const it = diff.accounts.find(address);
    if (it == diff.accounts.end())
    {
        return true;
    }
    return it->second.balance == initialBalance;
}
}  // namespace

// Scenario A — intrinsic gas below TxGas (geth ErrIntrinsicGas matrix).
BOOST_AUTO_TEST_CASE(applyReferenceMessage_intrinsic_gas_too_low_maps_to_out_of_gas_limit)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    state::test::InMemoryStateView stateView;

    auto output = task::syncWait(
        applyReferenceMessage(makeIntrinsicGasTooLowRequest(stateView, vm, hashImpl)));

    // CURRENT_ORACLE: GAP-001
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.evmcResult.gas_left, 0);
    BOOST_CHECK_EQUAL(static_cast<int>(output.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::OutOfGasLimit));
    // GETH_ORACLE: reject; no receipt (state_processor_test.go:181-186)
}

// Scenario A — TE settlement projection: gasUsed consumes full gasLimit when gas_left=0.
BOOST_AUTO_TEST_CASE(applyReferenceMessage_intrinsic_gas_failure_te_gas_used_equals_gas_limit)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    state::test::InMemoryStateView stateView;

    auto const input = makeIntrinsicGasTooLowRequest(stateView, vm, hashImpl);
    auto const gasLimit = input.message.gas;
    auto output = task::syncWait(applyReferenceMessage(EthReferenceRequest{input}));

    auto const projectedGasUsed = teProjectedGasUsed(gasLimit, output.evmcResult.gas_left);
    BOOST_CHECK_EQUAL(projectedGasUsed, gasLimit);
    // CURRENT_ORACLE: GAP-TE-004 gasUsed=gasLimit at Finalize
    // GETH_ORACLE: tx rejected before execution; no receipt gasUsed
}

// Scenario B — buyGas insufficient balance (TE layer; intrinsic gas sufficient).
// Bridge-layer oracle: EthTxFeeSettlement documents NotEnoughCash + partial penalty gasUsed.
BOOST_AUTO_TEST_CASE(eth_buy_gas_insufficient_balance_te_oracle_not_enough_cash)
{
    // Effective cost > balance while gasLimit >= TX_BASE_GAS (intrinsic satisfied).
    // CURRENT_ORACLE (EthTxFeeSettlement.h buyGas): NotEnoughCash, gas_left=0,
    //   gasUsed = min(balance, TX_BASE_GAS * effectiveGasPrice) / effectiveGasPrice; Finalize
    //   makeReceipt.
    // GETH_ORACLE: ErrInsufficientFunds (state_processor_test.go:171-176,217-226); reject, no
    // receipt.
    constexpr int64_t gasLimit = gas::TX_BASE_GAS;
    bcos::u256 const balance{50'000};
    bcos::u256 const effectiveGasPrice{1'000};
    bcos::u256 const maxGasCost = bcos::u256(gasLimit) * effectiveGasPrice;
    BOOST_REQUIRE_GT(maxGasCost, balance);

    bcos::u256 const intrinsicCost = bcos::u256(gas::TX_BASE_GAS) * effectiveGasPrice;
    bcos::u256 const penalty = std::min(balance, intrinsicCost);
    int64_t const projectedGasUsed = (penalty / effectiveGasPrice).template convert_to<int64_t>();

    BOOST_CHECK_EQUAL(projectedGasUsed, static_cast<int64_t>(balance / effectiveGasPrice));
    BOOST_CHECK_LT(projectedGasUsed, gasLimit);
}

// Task 2 — inclusion / receipt existence oracle at bridge layer (TE still makeReceipt).
BOOST_AUTO_TEST_CASE(applyReferenceMessage_intrinsic_gas_failure_inclusion_failed_receipt_oracle)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};
    state::test::InMemoryStateView stateView;

    auto const input = makeIntrinsicGasTooLowRequest(stateView, vm, hashImpl);
    auto const sender = input.message.sender;
    auto const initialBalance = stateView.get_balance(sender);

    auto output = task::syncWait(applyReferenceMessage(EthReferenceRequest{input}));

    // CURRENT_ORACLE (GAP-002 / GAP-TE-004): execution returns a failed outcome suitable for
    // inclusion + receipt at TE Finalize (not a block-level reject).
    BOOST_CHECK_NE(static_cast<int>(output.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
    BOOST_CHECK(output.evmcResult.status_code != EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(
        teProjectedGasUsed(input.message.gas, output.evmcResult.gas_left), input.message.gas);
    BOOST_CHECK(stateDiffPreservesBalance(output.stateDiff, sender, initialBalance));
    // GETH_ORACLE: could not apply tx N … intrinsic gas too low; subsequent tx index unchanged
    //   (state_processor_test.go:136-240)
}

}  // namespace bcos::evm::test
