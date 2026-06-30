#pragma once

#include "bcos-evm/eth/pipeline/IntrinsicGasDebit.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
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

    /// Post-EVM execution result normalization (included-vmerr, CREATE address, revert logs, etc.).
    /// Shared interface; chain-specific semantics live in orchestration adapters only.
    virtual void onPostExecuteNormalize(TxPipelineContext& ctx) const { (void)ctx; }

    /// Optional post-pipeline normalization (e.g. gas_left clamp on error paths).
    virtual void onPipelineComplete(TxPipelineContext& ctx) const { (void)ctx; }
};

}  // namespace bcos::evm
