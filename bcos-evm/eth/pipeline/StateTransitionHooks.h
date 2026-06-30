/*
 * Chain-specific pre-execution hooks for stateTransitionExecute (ADR-019 / ADR-030).
 *
 * Portable kernel interface: Eth/Fisco/OpStack supply concrete policies
 * (EthPrecheckPolicy, FiscoPrecheckPolicy, OpStackPrecheckPolicy) bound at
 * each chain's ApplyMessage entry. Error mapping lives in StateTransitionErrorPolicy.
 *
 * @file StateTransitionHooks.h
 */
#pragma once

#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"

namespace bcos::evm
{

/// Virtual hook table invoked by stateTransitionExecute in pipeline order.
/// Set ctx.earlyExit (and usually ctx.evmcResult) to short-circuit before EVM.
struct StateTransitionHooks
{
    virtual ~StateTransitionHooks() = default;

    /// Intrinsic debit mode/inputs for the kernel deductIntrinsicGas step (not a hook call).
    virtual DeductIntrinsicGasParams getIntrinsicGasParams() const = 0;

    // geth: TransactionToMessage — ADR-030
    /// Normalize ctx.message (CREATE address derivation, deposit tweaks, etc.).
    virtual void onNormalizeMessage(StateTransitionContext& ctx) const { (void)ctx; }

    // geth: preCheck (rules slice) — ADR-030
    /// Entry rules: unsupported tx type, auth list, revision gates. May set earlyExit.
    virtual void onPreCheckRules(StateTransitionContext& ctx) const { (void)ctx; }

    // geth: preCheck / buyGas — ADR-030
    /// Gas-limit / pool affordability before intrinsic debit. May set earlyExit.
    virtual void onPreCheckGasAffordable(StateTransitionContext& ctx) const { (void)ctx; }

    // geth: CanTransfer — ADR-030
    /// Balance and value transfer checks after intrinsic debit. May set earlyExit.
    virtual void onPreCheckCanTransfer(StateTransitionContext& ctx) const { (void)ctx; }

    /// Last-chance mutation of InnerExecuteInput after ctx.toInnerExecuteInput().
    virtual void onTuneInnerExecuteInput(InnerExecuteInput& input) const { (void)input; }

    // geth: innerExecute — ADR-030
    /// Run the EVM kernel; default delegates to innerExecute().
    virtual InnerExecuteOutput onInvokeInnerExecute(InnerExecuteInput&& input) const
    {
        return innerExecute(std::move(input));
    }
};

// Pipeline order (stateTransitionExecute):
//   onNormalizeMessage → onPreCheckRules → onPreCheckGasAffordable
//   → deductIntrinsicGas(getIntrinsicGasParams())   [kernel, not overridden here]
//   → onPreCheckCanTransfer
//   → toInnerExecuteInput → onTuneInnerExecuteInput → onInvokeInnerExecute

}  // namespace bcos::evm
