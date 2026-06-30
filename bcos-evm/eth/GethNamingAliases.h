/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief ADR-030 Tier A geth vocabulary inline aliases (eth kernel).
 * @file GethNamingAliases.h
 *
 * Portable eth/ symbols and their geth analogues — see ADR-030 for the full
 * bcos-evm ↔ go-ethereum map, including chain extension entry points.
 *
 * Kernel alias index (coexist with ADR-029 canonical names):
 *   stateTransitionExecute → runTxPipeline
 *   ChainPrecheckPolicy — pipelineCheckRules, preCheckGasAffordable, preCheckCanTransfer,
 *                         pipelineSetupMessage, pipelineInvokeEvmKernel
 *   deductIntrinsicGas → debitIntrinsicGas
 *   innerExecute → executeMessage
 *   prepareState → execution::warmTransactionEntry
 *   finalizeGasUsed → onPostExecuteNormalize (OrchestrationErrorPolicy)
 *   applyReferenceMessage / applyFiscoMessage / applyOpStackMessage — chain headers
 *   evmCall / evmCreate / evmDelegateCall / evmStaticCall → runCallFrame
 *
 * Stable ABI (Tier E — retain): executeMessage, fiscoExecute, ethReferenceExecute,
 *   opStackExecute, runTxPipeline, runExecutionFrame
 */

#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/pipeline/IntrinsicGasDebit.h"
#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include <evmc/evmc.h>
#include <cassert>

namespace bcos::evm
{

// geth: stateTransition.execute — ADR-030
inline void stateTransitionExecute(TxPipelineContext& ctx, ChainPrecheckPolicy const& precheck,
    OrchestrationErrorPolicy const& errorPolicy)
{
    runTxPipeline(ctx, precheck, errorPolicy);
}

// geth: IntrinsicGas — ADR-030
[[nodiscard]] inline IntrinsicGasDebitOutcome deductIntrinsicGas(
    evmc_message& message, IntrinsicGasDebitParams const& policy)
{
    return debitIntrinsicGas(message, policy);
}

// geth: innerExecute — ADR-030 Tier E forward to executeMessage
[[nodiscard]] inline ExecuteMessageOutput innerExecute(ExecuteMessageInput input)
{
    return executeMessage(std::move(input));
}

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
    assert(message.kind == EVMC_STATICCALL);
    return runCallFrame(ctx, message, scope, host);
}

}  // namespace execution

}  // namespace bcos::evm
