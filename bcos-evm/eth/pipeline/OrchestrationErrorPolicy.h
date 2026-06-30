#pragma once

#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include <exception>

namespace bcos::evm
{

/// Chain-specific error mapping for stateTransitionExecute early-exit and exception paths.
struct OrchestrationErrorPolicy
{
    virtual ~OrchestrationErrorPolicy() = default;

    virtual void onIntrinsicGasFailure(
        StateTransitionContext& ctx, IntrinsicDebitFailure failure) const = 0;

    virtual void onPipelineException(
        StateTransitionContext& ctx, std::exception_ptr exceptionPtr) const = 0;

    /// Post-EVM execution result normalization (included-vmerr, CREATE address, revert logs, etc.).
    /// Shared interface; chain-specific semantics live in orchestration adapters only.
    virtual void onPostExecuteNormalize(StateTransitionContext& ctx) const { (void)ctx; }

    /// Optional post-pipeline normalization (e.g. gas_left clamp on error paths).
    virtual void onPipelineComplete(StateTransitionContext& ctx) const { (void)ctx; }

    // geth: finalizeGasUsed — ADR-030 Tier A alias
    void finalizeGasUsed(StateTransitionContext& ctx) const { onPostExecuteNormalize(ctx); }
};

}  // namespace bcos::evm
