/*
 * Shared top-level transaction execution pipeline (ADR-019 / ADR-030).
 *
 * Canonical kernel entry for geth stateTransition.execute. Eth, Fisco, and
 * OpStack ApplyMessage paths converge here after constructing StateTransitionContext
 * and wiring execution environment via *ExecutionBundle.
 *
 * @file StateTransitionExecute.h
 */
#pragma once

#include "bcos-evm/eth/core/StateTransitionHooks.h"
#include "bcos-evm/eth/pipeline/StateTransitionErrorPolicy.h"

namespace bcos::evm
{

/// Run one top-level state transition: preCheck → intrinsic gas → EVM → result adopt.
///
/// Mutates \p ctx in place. On success or early-exit, read ctx.evmcResult,
/// ctx.exitKind, and ctx.kernelOutput. Chain-specific behavior is injected through
/// \p hooks (pre-execution) and \p errorPolicy (failure mapping / post-execute normalize).
///
/// Prerequisites:
///   - ctx.inputs.vm and ctx.inputs.hashImpl must be non-null
///   - *ExecutionBundle must have called ctx.wireExecutionEnvironment() before entry
///
/// Flow:
///   1. hooks.onNormalizeMessage          — geth: TransactionToMessage
///   2. hooks.onPreCheckRules           — entry rules; earlyExit → RulesRejected
///   3. hooks.onPreCheckGasAffordable    — buyGas / gas pool; earlyExit → GasAffordRejected
///   4. deductIntrinsicGas                — kernel intrinsic debit; fail → IntrinsicRejected
///   5. hooks.onPreCheckCanTransfer       — geth: CanTransfer; earlyExit → GasAffordRejected
///   6. ctx.toInnerExecuteInput()
///      → hooks.onTuneInnerExecuteInput
///      → hooks.onInvokeInnerExecute      — geth: innerExecute
///   7. adoptEvmcResult + EIP-7623 snapshot (when applicable)
///   8. errorPolicy.onFinalizeGasUsed     — included-vmerr, receipt fields, etc.
///
/// Exceptions inside the try block are mapped by errorPolicy.onException
/// (exitKind = ExceptionHandled). errorPolicy.onComplete runs on all paths via RAII guard.
void stateTransitionExecute(StateTransitionContext& ctx, StateTransitionHooks const& hooks,
    StateTransitionErrorPolicy const& errorPolicy);

}  // namespace bcos::evm
