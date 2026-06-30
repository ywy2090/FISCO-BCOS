/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief ADR-030 Tier A geth vocabulary inline aliases (eth kernel).
 * @file GethNamingAliases.h
 *
 * Portable eth/ symbols and their geth analogues — see ADR-030 for the full
 * bcos-evm ↔ go-ethereum map, including chain extension entry points.
 * Orchestration / host-bundle documentation type aliases: GethOrchestrationAliases.h.
 *
 * Kernel alias index (ADR-029 layer prefixes + ADR-030 geth names coexist):
 *
 *   Tier A (this header — eth kernel):
 *     stateTransitionExecute — canonical (geth stateTransition.execute; ADR-031)
 *     ChainPrecheckPolicy — preCheckRules, preCheckGasAffordable, preCheckCanTransfer,
 *                           normalizeMessage, pipelineInvokeEvmKernel
 *     deductIntrinsicGas — canonical (ADR-032 Wave 1 retired debitIntrinsicGas)
 *     innerExecute — canonical (geth innerExecute; ADR-031)
 *     prepareState → execution::warmTransactionEntry
 *     finalizeGasUsed → onPostExecuteNormalize (OrchestrationErrorPolicy)
 *     evmCall / evmCreate / evmDelegateCall / evmStaticCall → runCallFrame
 *
 *   Tier C (chain headers — geth ApplyMessage; exported apply*Message):
 *     applyReferenceMessage   (ApplyReferenceMessage.h)
 *     applyFiscoMessage       (ApplyFiscoMessage.h)
 *     applyOpStackMessage     (ApplyOpStackMessage.h)
 *
 *   ADR-033 (2026-06-30): disk paths aligned — InnerExecute, StateTransitionExecute,
 *     EvmCallFrame, DeductIntrinsicGas, IncludedTxVmerrNormalize (shims at old paths).
 */

#pragma once

#include "bcos-evm/eth/InnerExecute.h"
#include "bcos-evm/eth/execution/EvmCallFrame.h"
#include "bcos-evm/eth/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionExecute.h"
#include <evmc/evmc.h>
#include <cassert>

namespace bcos::evm
{

// geth: state.Prepare — ADR-030
inline void prepareState(state::State& state, bcos::evm_standard::RevisionConfig const& cfg,
    ChainCallTargetDispatcher const* chainPort, state::Transaction const& tx,
    state::BlockInfo const& block, state::TransactionProperties const& props,
    Eip2930AccessList const* accessList = nullptr, uint8_t web3TypedTxKind = 0,
    std::optional<evmc_address> createCodeAddress = std::nullopt)
{
    execution::warmTransactionEntry(
        state, cfg, chainPort, tx, block, props, accessList, web3TypedTxKind, createCodeAddress);
}

namespace execution
{

// geth: evm.Call — ADR-030
[[nodiscard]] inline FrameResult evmCall(
    FrameExecutionEnv& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    assert(message.kind == EVMC_CALL);
    return runCallFrame(ctx, message, scope, host);
}

// geth: evm.Create / Create2 — ADR-030
[[nodiscard]] inline FrameResult evmCreate(
    FrameExecutionEnv& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    assert(message.kind == EVMC_CREATE || message.kind == EVMC_CREATE2);
    return runCallFrame(ctx, message, scope, host);
}

// geth: evm.DelegateCall — ADR-030
[[nodiscard]] inline FrameResult evmDelegateCall(
    FrameExecutionEnv& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    assert(message.kind == EVMC_DELEGATECALL);
    return runCallFrame(ctx, message, scope, host);
}

// geth: evm.StaticCall — ADR-030
[[nodiscard]] inline FrameResult evmStaticCall(
    FrameExecutionEnv& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    assert(message.kind == EVMC_CALL && (message.flags & EVMC_STATIC) != 0);
    return runCallFrame(ctx, message, scope, host);
}

}  // namespace execution

}  // namespace bcos::evm
