#pragma once

#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-evm/eth/pipeline/TxPipelineHooks.h"

namespace bcos::evm
{

void runTxPipeline(TxPipelineContext& ctx, TxPipelineHooks const& hooks,
    OrchestrationErrorPolicy const& errorPolicy);

}  // namespace bcos::evm
