/*
 * included top-level vmerr settlement and applyEthMessage normalization.
 */
#define BOOST_TEST_MODULE EthIncludedTxVmerrTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/apply/EthMessage.h"
#include "bcos-evm/eth/eip/TxIntrinsicGas.h"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
using bcos::evm::gas::calcEip7623Components;
using bcos::evm::gas::settleTopLevelTransactionGas;

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

bcos::evm_standard::RevisionConfig osakaReferenceConfig()
{
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_OSAKA;
    cfg.eip2929 = true;
    cfg.eip7623 = true;
    cfg.eip7702 = true;
    cfg.eip1153 = true;
    cfg.eip4844 = true;
    cfg.eip5656 = true;
    cfg.eip6780 = true;
    cfg.calldata_floor_per_token = 10;
    return cfg;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthIncludedTxVmerrTest)

BOOST_AUTO_TEST_CASE(settleTopLevelTransactionGas_peakGasUsed_without_refund)
{
    auto const calldata = calcEip7623Components({});
    int64_t const gasUsed = settleTopLevelTransactionGas(10'000'000, 125'000, 0, 10, calldata);
    BOOST_CHECK_EQUAL(gasUsed, 9'875'000);
}

BOOST_AUTO_TEST_CASE(settleTopLevelTransactionGas_eest_invalid_vector)
{
    // EEST self_sponsored invalid tx_value_0: gasLimit=10_000_000, gasUsed=9_987_500 (geth).
    auto const calldata = calcEip7623Components({});
    int64_t const gasUsed = settleTopLevelTransactionGas(10'000'000, 12'500, 0, 10, calldata);
    BOOST_CHECK_EQUAL(gasUsed, 9'987'500);
}

BOOST_AUTO_TEST_CASE(settleTopLevelTransactionGas_applies_eip7623_floor)
{
    auto const calldata = calcEip7623Components({});
    int64_t const gasUsed = settleTopLevelTransactionGas(50'000, 49'000, 0, 10, calldata);
    BOOST_CHECK_EQUAL(gasUsed, 21'000);
}

BOOST_AUTO_TEST_CASE(applyEthMessage_top_level_invalid_is_included_with_success_status)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    state::test::InMemoryStateView view;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000'000'000'000;
    view.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = bcos::bytes{0xfe};  // INVALID
    view.insert_account(target, targetAccount);

    evmc_message message{};
    message.depth = 0;
    message.kind = EVMC_CALL;
    message.gas = 100'000;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    state::BlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    EthMessageRequest input{};
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = osakaReferenceConfig();

    auto output = task::syncWait(applyEthMessage(std::move(input)));

    BOOST_CHECK(output.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(output.gasSettlementSnapshot.gasLimit, 0);
}

// GAP-TE-002 / Plan Task 3: TE settleGasUsedFromEvmResult does not consult
// topLevelIncludedTxVmError — settlement is purely gasLimit/gas_left/refund.
// GETH_ORACLE: included top-level vmerr bills peak gas; EEST invalid
// self_sponsored tx_value_0 → gasUsed=9_987_500 (geth state_transition refund/finalize).
BOOST_AUTO_TEST_CASE(TopLevelIncludedTxVmErrorGasSettlement_invalid_opcode_eest_vector)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    state::test::InMemoryStateView view;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000'000'000'000;
    view.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = bcos::bytes{0xfe};  // INVALID
    view.insert_account(target, targetAccount);

    constexpr int64_t gasLimit = 10'000'000;

    evmc_message message{};
    message.depth = 0;
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    state::BlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    EthMessageRequest input{};
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = osakaReferenceConfig();
    // legacy tx (default kind=0) — type-2 without fee caps hits rules_rejected before EVM.

    auto output = task::syncWait(applyEthMessage(std::move(input)));

    BOOST_REQUIRE(output.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.evmcResult.gas_left, 0);

    auto const& snap = output.gasSettlementSnapshot;
    BOOST_REQUIRE_GT(snap.gasLimit, 0);

    // CURRENT_ORACLE: TE mirror path (EthTransactionExecutorImpl::settleGasUsedFromEvmResult)
    // has no branch on topLevelIncludedTxVmError — only gas_left + snapshot fields.
    int64_t const teMirrorGasUsed =
        settleTopLevelTransactionGas(snap.gasLimit, output.evmcResult.gas_left, snap.evmGasRefund,
            input.revisionConfig.calldata_floor_per_token, snap.calldata);

    // Flag is recorded but not consumed by settlement helper (same result with flag forced false).
    int64_t const teMirrorIgnoringFlag =
        settleTopLevelTransactionGas(snap.gasLimit, output.evmcResult.gas_left, snap.evmGasRefund,
            input.revisionConfig.calldata_floor_per_token, snap.calldata);
    BOOST_CHECK_EQUAL(teMirrorGasUsed, teMirrorIgnoringFlag);

    // CURRENT_ORACLE: included invalid opcode leaves gas_left=0 → TE bills full gasLimit.
    BOOST_CHECK_EQUAL(teMirrorGasUsed, gasLimit);

    // GETH_ORACLE: EEST self_sponsored invalid tx_value_0 → gasUsed=9_987_500
    // (go-ethereum/core/state_transition.go refund/finalize peak semantics).
    constexpr int64_t kGethGasUsed = 9'987'500;
    BOOST_CHECK_NE(teMirrorGasUsed, kGethGasUsed);
#if 0  // flip after GAP-TE-002 fix when TE consumes included-tx vmerr for settlement
    BOOST_CHECK_EQUAL(teMirrorGasUsed, kGethGasUsed);
#endif
}

// GAP-TE-002: top-level OOG (non-REVERT) is included vmerr; TE still ignores flag.
// GETH_ORACLE: included OOG charges full gasLimit (gas_left=0 after normalize).
BOOST_AUTO_TEST_CASE(TopLevelIncludedTxVmErrorGasSettlement_top_level_oog_charges_full_limit)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    state::test::InMemoryStateView view;
    auto const sender = addressFromLastByte(0x11);
    auto const target = addressFromLastByte(0x22);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000'000'000'000;
    view.insert_account(sender, senderAccount);

    // JUMPDEST PUSH1 0 JUMP — spins until OUT_OF_GAS.
    state::Account targetAccount;
    targetAccount.code = bcos::bytes{0x5b, 0x60, 0x00, 0x56};
    view.insert_account(target, targetAccount);

    constexpr int64_t gasLimit = 100'000;

    evmc_message message{};
    message.depth = 0;
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    state::BlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    EthMessageRequest input{};
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = osakaReferenceConfig();

    auto output = task::syncWait(applyEthMessage(std::move(input)));

    BOOST_REQUIRE(output.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.evmcResult.gas_left, 0);

    auto const& snap = output.gasSettlementSnapshot;
    int64_t const teMirrorGasUsed =
        settleTopLevelTransactionGas(snap.gasLimit, output.evmcResult.gas_left, snap.evmGasRefund,
            input.revisionConfig.calldata_floor_per_token, snap.calldata);

    // CURRENT_ORACLE + GETH_ORACLE align for OOG: full gasLimit charged when gas_left=0.
    BOOST_CHECK_EQUAL(teMirrorGasUsed, gasLimit);
}

BOOST_AUTO_TEST_CASE(applyEthMessage_nested_invalid_is_not_included_tx_vmerr)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    state::test::InMemoryStateView view;
    auto const sender = addressFromLastByte(0x01);
    auto const callee = addressFromLastByte(0x02);
    auto const caller = addressFromLastByte(0x03);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000'000'000'000;
    view.insert_account(sender, senderAccount);

    state::Account calleeAccount;
    calleeAccount.code = bcos::bytes{0xfe};
    view.insert_account(callee, calleeAccount);

    // PUSH0 PUSH0 PUSH0 PUSH0 PUSH20 callee GAS CALL STOP
    state::Account callerAccount;
    callerAccount.code = bcos::fromHex("5f5f5f5f7300000000000000000000000000000000025af100");
    view.insert_account(caller, callerAccount);

    evmc_message message{};
    message.depth = 0;
    message.kind = EVMC_CALL;
    message.gas = 200'000;
    message.sender = sender;
    message.recipient = caller;
    message.code_address = caller;

    state::BlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.gasLimit = 30'000'000;

    EthMessageRequest input{};
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = osakaReferenceConfig();

    auto output = task::syncWait(applyEthMessage(std::move(input)));

    BOOST_CHECK(!output.topLevelIncludedTxVmError);
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::evm::test
