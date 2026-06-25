#pragma once

#include "bcos-evm/eth/pipeline/DebitIntrinsicGas.h"
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
    /// ADR-015: interface is shared; chain semantics live in Eth/Fisco/OpStack adapters only.
    virtual void onPostExecuteNormalize(TxPipelineContext& ctx) const { (void)ctx; }

    /// Optional post-pipeline normalization (e.g. FISCO negative gas_left clamp).
    virtual void onPipelineComplete(TxPipelineContext& ctx) const { (void)ctx; }
};

}  // namespace bcos::evm
