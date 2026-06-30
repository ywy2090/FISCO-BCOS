#pragma once

#include "bcos-evm/eth/InnerExecute.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"

namespace bcos::evm
{

/// Chain-specific pre-execution checks for stateTransitionExecute (setup, rules, gas, balance,
/// tuning).
struct ChainPrecheckPolicy
{
    virtual ~ChainPrecheckPolicy() = default;

    virtual DeductIntrinsicGasParams deductIntrinsicGasParams() const = 0;

    virtual void pipelineSetupMessage(StateTransitionContext& ctx) const { (void)ctx; }

    virtual void pipelineCheckRules(StateTransitionContext& ctx) const { (void)ctx; }

    virtual void pipelineCheckGasAffordable(StateTransitionContext& ctx) const { (void)ctx; }

    virtual void pipelineCheckBalance(StateTransitionContext& ctx) const { (void)ctx; }

    virtual void pipelineTuneKernelInput(InnerExecuteInput& input) const { (void)input; }

    virtual InnerExecuteOutput pipelineInvokeEvmKernel(InnerExecuteInput&& input) const
    {
        return innerExecute(std::move(input));
    }

    // geth: preCheck slices — ADR-030 Tier A aliases (forward to ADR-029 canonical names)
    void preCheckRules(StateTransitionContext& ctx) const { pipelineCheckRules(ctx); }

    void preCheckGasAffordable(StateTransitionContext& ctx) const
    {
        pipelineCheckGasAffordable(ctx);
    }

    void preCheckCanTransfer(StateTransitionContext& ctx) const { pipelineCheckBalance(ctx); }

    // geth: TransactionToMessage — ADR-030
    void normalizeMessage(StateTransitionContext& ctx) const { pipelineSetupMessage(ctx); }
};

}  // namespace bcos::evm
