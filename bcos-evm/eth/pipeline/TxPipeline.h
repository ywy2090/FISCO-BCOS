#pragma once

#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"

namespace bcos::evm
{

void runTxPipeline(TxPipelineContext& ctx, ChainPrecheckPolicy const& precheckPolicy,
    OrchestrationErrorPolicy const& errorPolicy);

}  // namespace bcos::evm
