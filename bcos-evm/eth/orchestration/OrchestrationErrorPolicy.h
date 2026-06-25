#pragma once

#include "bcos-evm/eth/orchestration/DebitIntrinsicGas.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include <exception>

namespace bcos::evm
{

/// Chain-specific error mapping for runTxPipeline early-exit and exception paths.
struct OrchestrationErrorPolicy
{
    virtual ~OrchestrationErrorPolicy() = default;

    virtual void onIntrinsicGasFailure(
        TxPipelineContext& ctx, IntrinsicDebitFailure failure) const = 0;

    virtual void onPipelineException(
        TxPipelineContext& ctx, std::exception_ptr exceptionPtr) const = 0;

    /// Optional post-pipeline normalization (e.g. FISCO negative gas_left clamp).
    virtual void onPipelineComplete(TxPipelineContext& ctx) const { (void)ctx; }
};

}  // namespace bcos::evm
