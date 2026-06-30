#pragma once

#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"

namespace bcos::evm
{

// geth: stateTransition.execute — ADR-030 / ADR-031 canonical
void stateTransitionExecute(StateTransitionContext& ctx, ChainPrecheckPolicy const& precheckPolicy,
    OrchestrationErrorPolicy const& errorPolicy);

}  // namespace bcos::evm
