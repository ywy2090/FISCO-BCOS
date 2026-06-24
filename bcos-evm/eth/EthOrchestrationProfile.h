#pragma once

#include "bcos-evm/eth/EthPipelineHookBinder.h"

namespace bcos::evm
{
using EthOrchestrationProfile [[deprecated("use EthPipelineHookBinder")]] = EthPipelineHookBinder;
}  // namespace bcos::evm
