#pragma once

#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-evm/eth/pipeline/StateTransitionHooks.h"

namespace bcos::evm
{

// geth: stateTransition.execute — ADR-030 / ADR-031 canonical
void stateTransitionExecute(StateTransitionContext& ctx, StateTransitionHooks const& hooks,
    OrchestrationErrorPolicy const& errorPolicy);

}  // namespace bcos::evm
