#pragma once

#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-evm/opstack/OpStackPipelineInternals.h"

namespace bcos::evm
{

struct OpStackOrchestrationErrorPolicy : OrchestrationErrorPolicy
{
    void onIntrinsicGasFailure(
        StateTransitionContext& ctx, IntrinsicDebitFailure /*failure*/) const override
    {
        ctx.evmcResult = makeOutOfGasLimitResult();
    }

    void onPipelineException(
        StateTransitionContext& ctx, std::exception_ptr /*exceptionPtr*/) const override
    {
        ctx.evmcResult = makeInternalErrorResult();

        if (ctx.state.has_checkpoint())
        {
            ctx.state.revert();
        }
    }
};

}  // namespace bcos::evm
