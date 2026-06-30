#pragma once

#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"

namespace bcos::evm
{

/// Portable hooks injected into stateTransitionExecute (message setup, preCheck slices, kernel
/// invoke).
struct StateTransitionHooks
{
    virtual ~StateTransitionHooks() = default;

    virtual DeductIntrinsicGasParams getIntrinsicGasParams() const = 0;

    // geth: TransactionToMessage — ADR-030
    virtual void onNormalizeMessage(StateTransitionContext& ctx) const { (void)ctx; }

    // geth: preCheck (rules slice) — ADR-030
    virtual void onPreCheckRules(StateTransitionContext& ctx) const { (void)ctx; }

    // geth: preCheck / buyGas — ADR-030
    virtual void onPreCheckGasAffordable(StateTransitionContext& ctx) const { (void)ctx; }

    // geth: CanTransfer — ADR-030
    virtual void onPreCheckCanTransfer(StateTransitionContext& ctx) const { (void)ctx; }

    virtual void onTuneInnerExecuteInput(InnerExecuteInput& input) const { (void)input; }

    // geth: innerExecute — ADR-030
    virtual InnerExecuteOutput onInvokeInnerExecute(InnerExecuteInput&& input) const
    {
        return innerExecute(std::move(input));
    }
};

}  // namespace bcos::evm
