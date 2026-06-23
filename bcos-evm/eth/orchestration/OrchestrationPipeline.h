#pragma once

#include "bcos-evm/eth/orchestration/OrchestrationHooks.h"

namespace bcos::evm
{

void runOrchestration(OrchestrationContext& ctx, OrchestrationHooks const& hooks);

}  // namespace bcos::evm
