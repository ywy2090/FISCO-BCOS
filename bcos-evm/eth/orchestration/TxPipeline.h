#pragma once

#include "bcos-evm/eth/orchestration/OrchestrationErrorPolicy.h"
#include "bcos-evm/eth/orchestration/TxPipelineHooks.h"

namespace bcos::evm
{

void runTxPipeline(TxPipelineContext& ctx, TxPipelineHooks const& hooks,
    OrchestrationErrorPolicy const& errorPolicy);

}  // namespace bcos::evm
