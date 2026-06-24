#pragma once

#include "bcos-evm/bcos/FiscoPipelineHookBinder.h"

namespace bcos::evm
{
using FiscoOrchestrationProfile [[deprecated("use FiscoPipelineHookBinder")]] =
    FiscoPipelineHookBinder;
}  // namespace bcos::evm
