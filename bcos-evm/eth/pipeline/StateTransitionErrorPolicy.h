#pragma once

#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"
#include <exception>

namespace bcos::evm
{

/// Chain-specific error mapping for stateTransitionExecute early-exit and exception paths.
struct StateTransitionErrorPolicy
{
    virtual ~StateTransitionErrorPolicy() = default;

    virtual void onIntrinsicGasFailure(
        StateTransitionContext& ctx, IntrinsicDebitFailure failure) const = 0;

    virtual void onException(
        StateTransitionContext& ctx, std::exception_ptr exceptionPtr) const = 0;

    /// Post-EVM execution result normalization (included-vmerr, CREATE address, revert logs, etc.).
    virtual void onFinalizeGasUsed(StateTransitionContext& ctx) const { (void)ctx; }

    /// Optional post-pipeline normalization (e.g. gas_left clamp on error paths).
    virtual void onComplete(StateTransitionContext& ctx) const { (void)ctx; }
};

}  // namespace bcos::evm
