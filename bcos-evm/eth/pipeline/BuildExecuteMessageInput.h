#pragma once

#include "bcos-evm/eth/pipeline/ExecutionSession.h"

namespace bcos::evm
{

[[deprecated(
    "Use ExecutionSession::toExecuteMessageInput via ctx.session")]] inline ExecuteMessageInput
buildExecuteMessageInput(TxPipelineContext& ctx)
{
    if (ctx.session == nullptr)
    {
        throw std::invalid_argument(
            "buildExecuteMessageInput requires wired ExecutionSession on ctx.session");
    }
    return ctx.session->toExecuteMessageInput(ctx);
}

}  // namespace bcos::evm
