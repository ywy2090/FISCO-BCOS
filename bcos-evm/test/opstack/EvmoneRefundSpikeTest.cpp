/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Day-0 spike (Task 0): evmone gas_left vs gas_refund semantics under EVMC_PRAGUE.
 *
 * SPIKE OUTCOME (see docs/superpowers/plans/2026-06-18-opstack-isthmus-spike.md):
 *   Branch A — DEFERRED_REFUND: evmc_result.gas_left is pre-refund remaining gas;
 *              settlement must apply min(gas_refund, peakGasUsed/5) explicitly (Q10-A).
 *   Branch B — PRE_APPLIED_REFUND: gas_left already includes refund credit; settlement
 *              must NOT double-apply gas_refund (would require host shim in Task 6).
 *
 * Classification uses the discriminator:
 *   peakGasUsed = gasLimit - gas_left
 *   cappedRefund = min(gas_refund, peakGasUsed / 5)
 *   If PRE_APPLIED: peakGasUsed ≈ executionCost - cappedRefund (lower than raw execution).
 *   If DEFERRED:    peakGasUsed ≈ raw execution cost (refund not yet credited to gas_left).
 */

#define BOOST_TEST_MODULE EvmoneRefundSpikeTest

#include "bcos-evm/eth/eip/Eip2929StorageGas.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/state/State.hpp"
#include "helpers/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <iostream>
#include <string>

namespace bcos::evm::opstack::test
{
namespace
{
constexpr int64_t kGasLimit = 1'000'000;
constexpr int64_t kExpectedSstoreClearRefund = gas::SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529;

// PUSH1 0 PUSH1 0 SSTORE STOP
constexpr std::string_view kSstoreClearBytecode = "600060005500";

enum class RefundSemantics
{
    DEFERRED_REFUND,
    PRE_APPLIED_REFUND,
    INDETERMINATE
};

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_bytes32 valueFromLastByte(uint8_t value)
{
    evmc_bytes32 out{};
    out.bytes[31] = value;
    return out;
}

bcos::bytes hexBytes(std::string_view hex)
{
    return bcos::fromHex(hex);
}

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}

RefundSemantics classifyRefundSemantics(
    int64_t gasLimit, int64_t gasLeftWithRefund, int64_t gasRefund, int64_t gasLeftBaseline)
{
    if (gasRefund <= 0)
    {
        return RefundSemantics::INDETERMINATE;
    }

    int64_t const peakWithRefund = gasLimit - gasLeftWithRefund;
    int64_t const cappedRefund = std::min(gasRefund, peakWithRefund / gas::REFUND_QUOTIENT_EIP3529);
    int64_t const executionCostDelta = peakWithRefund - (gasLimit - gasLeftBaseline);
    int64_t const gasLeftDelta = gasLeftBaseline - gasLeftWithRefund;

    // DEFERRED: gas_left excludes refund; baseline delta equals execution cost delta.
    // PRE_APPLIED: gas_left includes cappedRefund; baseline delta is smaller by ~cappedRefund.
    if (gasLeftDelta + cappedRefund <= executionCostDelta)
    {
        return RefundSemantics::PRE_APPLIED_REFUND;
    }
    return RefundSemantics::DEFERRED_REFUND;
}

char const* semanticsLabel(RefundSemantics semantics)
{
    switch (semantics)
    {
    case RefundSemantics::DEFERRED_REFUND:
        return "DEFERRED_REFUND";
    case RefundSemantics::PRE_APPLIED_REFUND:
        return "PRE_APPLIED_REFUND";
    case RefundSemantics::INDETERMINATE:
        return "INDETERMINATE";
    }
    return "UNKNOWN";
}

struct RunOutcome
{
    evmc_status_code status{EVMC_INTERNAL_ERROR};
    int64_t gasLeft{0};
    int64_t gasRefund{0};
    evmc_bytes32 storage{};
};

RunOutcome runSstoreClear(bool prefillNonZero)
{
    state::test::InMemoryStateView view;
    auto const contract = addressFromLastByte(0x01);
    auto const sender = addressFromLastByte(0xaa);
    auto const slotKey = evmc_bytes32{};
    auto const nonZeroValue = valueFromLastByte(0x01);

    state::Account contractAccount;
    contractAccount.code = hexBytes(kSstoreClearBytecode);
    if (prefillNonZero)
    {
        contractAccount.storage[slotKey] = nonZeroValue;
    }
    view.insert_account(contract, contractAccount);

    state::State state(view);
    evmc_tx_context txContext{};
    txContext.tx_origin = sender;
    txContext.block_gas_limit = 30'000'000;

    evmc::VM vm{evmc_create_evmone()};
    bcos::evm::RevisionConfig cfg{.revision = EVMC_PRAGUE, .eip2929 = true};
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = kGasLimit;
    msg.sender = sender;
    msg.recipient = contract;
    msg.code_address = contract;
    msg.value = {};

    auto const result = vm.execute(
        host, EVMC_PRAGUE, msg, contractAccount.code.data(), contractAccount.code.size());

    RunOutcome outcome;
    outcome.status = result.status_code;
    outcome.gasLeft = result.gas_left;
    outcome.gasRefund = result.gas_refund;
    outcome.storage = state.get_storage(contract, slotKey);
    return outcome;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EvmoneRefundSpikeTest)

BOOST_AUTO_TEST_CASE(SstoreClear_recordsGasLeftAndRefund)
{
    auto const clearOutcome = runSstoreClear(true);
    auto const baselineOutcome = runSstoreClear(false);

    BOOST_REQUIRE_EQUAL(clearOutcome.status, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(baselineOutcome.status, EVMC_SUCCESS);

    int64_t const gasLeft = clearOutcome.gasLeft;
    int64_t const gasRefund = clearOutcome.gasRefund;
    int64_t const baselineGasLeft = baselineOutcome.gasLeft;
    int64_t const peakGasUsed = kGasLimit - gasLeft;
    int64_t const cappedRefund = std::min(gasRefund, peakGasUsed / gas::REFUND_QUOTIENT_EIP3529);
    int64_t const gasRemainingIfDeferred = gasLeft + cappedRefund;
    int64_t const gasUsedIfDeferred = kGasLimit - gasRemainingIfDeferred;

    std::cout << "=== EvmoneRefundSpike (EVMC_PRAGUE) ===\n"
              << "  gas_limit:              " << kGasLimit << '\n'
              << "  gas_left (clear):       " << gasLeft << '\n'
              << "  gas_left (baseline):    " << baselineGasLeft << '\n'
              << "  gas_refund:             " << gasRefund << '\n'
              << "  peak_gas_used:          " << peakGasUsed << " (gas_limit - gas_left)\n"
              << "  capped_refund (EIP3529): " << cappedRefund << " min(refund, peak/5)\n"
              << "  gas_remaining (deferred): " << gasRemainingIfDeferred
              << " (gas_left + capped_refund)\n"
              << "  gas_used (deferred):    " << gasUsedIfDeferred << '\n';

    BOOST_TEST_MESSAGE(
        "gas_left=" << gasLeft << " gas_refund=" << gasRefund << " peakGasUsed=" << peakGasUsed
                    << " cappedRefund=" << cappedRefund << " baselineGasLeft=" << baselineGasLeft);

    BOOST_CHECK_EQUAL(gasRefund, kExpectedSstoreClearRefund);
    BOOST_CHECK(state::Bytes32Equal{}(clearOutcome.storage, evmc_bytes32{}));
    BOOST_CHECK(state::Bytes32Equal{}(baselineOutcome.storage, evmc_bytes32{}));

    auto const semantics = classifyRefundSemantics(kGasLimit, gasLeft, gasRefund, baselineGasLeft);
    std::cout << "  spike_classification:   " << semanticsLabel(semantics) << '\n'
              << "========================================\n";

    BOOST_TEST_MESSAGE("refund_semantics=" << semanticsLabel(semantics));

    BOOST_CHECK_MESSAGE(semantics != RefundSemantics::PRE_APPLIED_REFUND,
        "evmone PRE_APPLIED_REFUND detected — Task 6 will need a host shim to subtract "
        "cappedRefund from gas_left before settlement");
    BOOST_CHECK(semantics == RefundSemantics::DEFERRED_REFUND);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::opstack::test
