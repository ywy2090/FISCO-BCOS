#define BOOST_TEST_MODULE OpStackTxLifecycleCharacterizationTest

// C1: characterization oracles via runOpStackTxLifecycle (deep module interface).

#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackTxLifecycle.h"
#include "bcos-evm/opstack/fee/OpStackFloorGas.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "bcos-protocol/TransactionStatus.h"
#include "opstack/helpers/OpStackLifecycleTestHelpers.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
constexpr int64_t kLifecycleNormalGasLimit = 100'000;
constexpr u256 kLifecycleSenderInitialBalance{2'000'000};
constexpr u256 kLifecycleEffectiveGasPrice{7};
constexpr u256 kLifecycleBaseFee{2};
constexpr u256 kLifecycleL1Fee{50};

u256 expectedSenderBalanceAfterNormalLifecycle(
    u256 initialBalance, uint64_t gasUsed, uint64_t gasRemaining)
{
    auto const buyGasDebit =
        u256(kLifecycleNormalGasLimit) * kLifecycleEffectiveGasPrice + kLifecycleL1Fee;
    return initialBalance - buyGasDebit + u256(gasRemaining) * kLifecycleEffectiveGasPrice;
}
}  // namespace

BOOST_AUTO_TEST_CASE(lifecycle_normal_success_routes_fees_and_gas_pool)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x01);
    auto const recipient = lifecycleAddressFromLastByte(0x02);
    auto const coinbase = lifecycleAddressFromLastByte(0x99);
    setLifecycleOpFeeParams(stateView);

    stateView.insert_account(
        sender, state::Account{.balance = kLifecycleSenderInitialBalance, .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    auto input = makeLifecycleNormalInput(
        stateView, vm, hash, sender, recipient, kLifecycleNormalGasLimit, gasPoolSpy);
    input.blockInfo.coinbase = coinbase;

    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, output.gasUsed));
    auto const expectedGas =
        postExecuteGasSettlement(static_cast<uint64_t>(kLifecycleNormalGasLimit),
            static_cast<uint64_t>(std::max<int64_t>(0, output.evmcResult.gas_left)),
            output.evmcResult.gas_refund > 0 ? static_cast<uint64_t>(output.evmcResult.gas_refund) :
                                               uint64_t{0},
            0u);
    auto const tipPerGas = kLifecycleEffectiveGasPrice - kLifecycleBaseFee;

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.gasUsed, static_cast<int64_t>(expectedGas.gasUsed));
    BOOST_CHECK_GT(output.gasUsed, int64_t{0});
    BOOST_REQUIRE(output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.l1Fee, kLifecycleL1Fee);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastSubGas, static_cast<uint64_t>(kLifecycleNormalGasLimit));
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnRemaining, expectedGas.gasRemaining);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnUsed, gasUsed);

    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender),
        expectedSenderBalanceAfterNormalLifecycle(
            kLifecycleSenderInitialBalance, gasUsed, expectedGas.gasRemaining));
    BOOST_CHECK_EQUAL(
        lifecycleBalanceFromDiff(output.stateDiff, coinbase), u256(gasUsed) * tipPerGas);
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, OP_BASE_FEE_RECIPIENT),
        u256(gasUsed) * kLifecycleBaseFee);
    BOOST_CHECK_EQUAL(
        lifecycleBalanceFromDiff(output.stateDiff, OP_L1_FEE_RECIPIENT), kLifecycleL1Fee);
}

BOOST_AUTO_TEST_CASE(lifecycle_normal_intrinsic_reject_gas_used_zero_and_returns_full_gas_pool)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x03);
    auto const recipient = lifecycleAddressFromLastByte(0x04);
    setLifecycleOpFeeParams(stateView);

    stateView.insert_account(sender, state::Account{.balance = u256(300'000), .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    evmc_message probe{};
    probe.kind = EVMC_CALL;
    auto const gasBelowIntrinsic = lifecycleGasBelowIntrinsic(probe);

    auto input = makeLifecycleNormalInput(
        stateView, vm, hash, sender, recipient, gasBelowIntrinsic, gasPoolSpy);
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender), u256(300'000));
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnRemaining, static_cast<uint64_t>(gasBelowIntrinsic));
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnUsed, 0u);
}

BOOST_AUTO_TEST_CASE(lifecycle_normal_gas_afford_reject_aborts_without_settle_or_receipt_fees)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x07);
    auto const recipient = lifecycleAddressFromLastByte(0x08);
    setLifecycleOpFeeParams(stateView);

    auto const initialBalance = u256(300'000);
    stateView.insert_account(sender, state::Account{.balance = initialBalance, .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    bcos::bytes calldata(100, 0xff);
    auto const floor = floorDataGas(bcos::ref(calldata));
    auto const gasBelowFloor = static_cast<int64_t>(floor - 1);

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    auto input =
        makeLifecycleNormalInput(stateView, vm, hash, sender, recipient, gasBelowFloor, gasPoolSpy);
    input.message.input_data = calldata.data();
    input.message.input_size = calldata.size();
    input.message.gas = gasBelowFloor;

    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender), initialBalance);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnRemaining, static_cast<uint64_t>(gasBelowFloor));
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnUsed, 0u);
}

BOOST_AUTO_TEST_CASE(lifecycle_normal_buy_gas_fail_releases_gas_pool_without_settle)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x05);
    auto const recipient = lifecycleAddressFromLastByte(0x06);
    stateView.insert_account(sender, state::Account{.balance = u256(10), .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    auto input =
        makeLifecycleNormalInput(stateView, vm, hash, sender, recipient, 100'000, gasPoolSpy);
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status, protocol::TransactionStatus::NotEnoughCash);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnRemaining, 100'000u);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnUsed, 0u);
    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
}

BOOST_AUTO_TEST_CASE(lifecycle_deposit_success_mint_deposit_nonce_and_nonce_bump)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x31);
    auto const recipient = lifecycleAddressFromLastByte(0x32);
    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = 3});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    auto input = makeLifecycleDepositInput(
        stateView, vm, hash, sender, recipient, 50'000, gasPoolSpy, u256(100));
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender), u256(100));
    BOOST_CHECK_EQUAL(lifecycleNonceFromDiff(output.stateDiff, sender, 3), 4u);
    BOOST_REQUIRE(output.receiptMeta.depositNonce.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.depositNonce, 3u);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
}

BOOST_AUTO_TEST_CASE(lifecycle_deposit_gas_pool_reject_skips_pipeline)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x33);
    auto const recipient = lifecycleAddressFromLastByte(0x34);
    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = 1});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;
    gasPoolSpy.subGasResult = false;

    auto input = makeLifecycleDepositInput(
        stateView, vm, hash, sender, recipient, 50'000, gasPoolSpy, u256(100));
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_CHECK_EQUAL(gasPoolSpy.returnGasCallCount, 0);
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender), u256(0));
}

BOOST_AUTO_TEST_CASE(lifecycle_deposit_revert_keeps_mint_and_bumps_nonce)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x41);
    auto const recipient = lifecycleAddressFromLastByte(0x42);
    stateView.insert_account(sender, state::Account{.balance = 50, .nonce = 7});
    installAlwaysRevertCode(stateView, recipient);

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    auto input = makeLifecycleDepositInput(
        stateView, vm, hash, sender, recipient, 50'000, gasPoolSpy, u256(100));
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender), u256(150));
    BOOST_CHECK_EQUAL(lifecycleNonceFromDiff(output.stateDiff, sender, 7), 8u);
    auto const expectedGas = postExecuteGasSettlement(
        50'000u, static_cast<uint64_t>(std::max<int64_t>(0, output.evmcResult.gas_left)), 0u, 0u);
    BOOST_CHECK_EQUAL(output.gasUsed, static_cast<int64_t>(expectedGas.gasUsed));
    BOOST_CHECK_LT(output.gasUsed, 50'000);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
}

BOOST_AUTO_TEST_CASE(lifecycle_deposit_intrinsic_reject_uses_gas_limit_and_reverts_journal)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x51);
    auto const recipient = lifecycleAddressFromLastByte(0x52);
    stateView.insert_account(sender, state::Account{.balance = 0, .nonce = 5});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    evmc_message probe{};
    probe.kind = EVMC_CALL;
    auto const gasBelowIntrinsic = lifecycleGasBelowIntrinsic(probe);

    auto input = makeLifecycleDepositInput(
        stateView, vm, hash, sender, recipient, gasBelowIntrinsic, gasPoolSpy, u256(123));
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.gasUsed, gasBelowIntrinsic);
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender), u256(123));
    BOOST_CHECK_EQUAL(lifecycleNonceFromDiff(output.stateDiff, sender, 5), 6u);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.subGasCallCount, 1);
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnRemaining, 0u);
    BOOST_CHECK_EQUAL(gasPoolSpy.lastReturnUsed, static_cast<uint64_t>(gasBelowIntrinsic));
}

// GAP-002 / GAP-TE-005 — entry failure inclusion oracle (receipt existence at TE; not settlement).
// CURRENT_ORACLE: lifecycle returns failed outcome → TE emits failed receipt (included tx).
// GETH_ORACLE: block processor rejects; no receipt (state_processor_test.go:181-186).
BOOST_AUTO_TEST_CASE(lifecycle_normal_intrinsic_reject_inclusion_failed_receipt_oracle)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x0a);
    auto const recipient = lifecycleAddressFromLastByte(0x0b);
    setLifecycleOpFeeParams(stateView);

    stateView.insert_account(sender, state::Account{.balance = u256(300'000), .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    evmc_message probe{};
    probe.kind = EVMC_CALL;
    auto const gasBelowIntrinsic = lifecycleGasBelowIntrinsic(probe);

    auto input = makeLifecycleNormalInput(
        stateView, vm, hash, sender, recipient, gasBelowIntrinsic, gasPoolSpy);
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_NE(static_cast<int>(output.evmcResult.status),
        static_cast<int>(protocol::TransactionStatus::None));
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(lifecycleBalanceFromDiff(output.stateDiff, sender), u256(300'000));
}

// GAP-TE-005 — buyGas entry failure still yields includable failed receipt at TE.
// CURRENT_ORACLE: NotEnoughCash lifecycle result (included).
// GETH_ORACLE: ErrInsufficientFunds reject (state_processor_test.go:171-176).
BOOST_AUTO_TEST_CASE(lifecycle_normal_buy_gas_fail_inclusion_failed_receipt_oracle)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = lifecycleAddressFromLastByte(0x0c);
    auto const recipient = lifecycleAddressFromLastByte(0x0d);
    stateView.insert_account(sender, state::Account{.balance = u256(10), .nonce = 0});
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    LifecycleFakeHash hash;
    LifecycleGasPoolSpy gasPoolSpy;

    auto input =
        makeLifecycleNormalInput(stateView, vm, hash, sender, recipient, 100'000, gasPoolSpy);
    auto output = task::syncWait(runOpStackTxLifecycle(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status, protocol::TransactionStatus::NotEnoughCash);
    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
    BOOST_REQUIRE_EQUAL(gasPoolSpy.returnGasCallCount, 1);
}
}  // namespace bcos::evm::test
