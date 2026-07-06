/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Post-execution gas metering and deposit finalization.
 * @file OpStackTxFinalize.h
 *
 * Normal txs: compute gasUsed/gasRemaining/maxUsedGas via postExecuteGasSettlement
 * (combines gas_left, SSTORE refund, Regolith+ floorDataGas).
 *
 * Deposit txs: op-geth deposit rules — always bump depositor nonce; success commits
 * state, revert discards execution changes but still advances nonce.
 *
 * OpStackTxFinalizeResult fields:
 *   gasUsed       — gas charged to sender / reported on receipt
 *   gasRemaining  — unused portion of gas limit returned to block pool
 *   maxUsedGas    — max(gasUsed, floorDataGas); operator fee basis (Isthmus+)
 */

#pragma once

#include <bcos-task/Task.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <functional>

namespace bcos::evm
{

enum class StateTransitionExitKind;
class StateTransitionContext;
struct OpStackFeeParams;
struct OpStackMessageResult;
struct OpStackFeeSidecar;

struct GasPoolHooks
{
    /// Block-level gas pool acquire (applyOpStackMessage).
    std::function<bool(uint64_t)> subGas;
    /// Return unused gas limit and report consumed gas after settlement.
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> returnGas;
};

struct OpStackTxFinalizeResult
{
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
    uint64_t maxUsedGas{0};
};

/// Intrinsic or gas-afford rejection before EVM runs; no gas charged.
bool isNormalPreExecutionReject(StateTransitionExitKind exitKind) noexcept;

/// Revert checkpoint and release full gas limit back to block pool (buyGas failure / pre-exit).
void abortNormalAfterBuyGas(StateTransitionContext& ctx, GasPoolHooks const& gasPool,
    OpStackMessageResult& output, int64_t originalGasLimit);

OpStackTxFinalizeResult finalizeNormal(StateTransitionContext const& ctx,
    OpStackFeeSidecar const& sidecar, StateTransitionExitKind exitKind);

/// Deposit path: success commits state; failure reverts execution but always bumps sender nonce.
OpStackTxFinalizeResult finalizeDeposit(
    StateTransitionContext& ctx, StateTransitionExitKind exitKind, evmc_status_code evmStatus);

task::Task<OpStackTxFinalizeResult> settleDeposit(StateTransitionContext& ctx,
    StateTransitionExitKind exitKind, evmc_status_code evmStatus, GasPoolHooks const& gasPool);

}  // namespace bcos::evm
