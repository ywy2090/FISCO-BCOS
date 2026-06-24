#pragma once

#include "bcos-evm/eth/orchestration/TxPipelineHooks.h"

namespace bcos::evm
{

void runTxPipeline(TxPipelineContext& ctx, TxPipelineHooks const& hooks);

}  // namespace bcos::evm
