#pragma once

#include "bcos-evm/opstack/OpStackPipelineHookBinder.h"

namespace bcos::evm
{
using OpStackOrchestrationProfile [[deprecated("use OpStackPipelineHookBinder")]] =
    OpStackPipelineHookBinder;
}  // namespace bcos::evm
