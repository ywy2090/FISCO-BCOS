/*
 *  ADR-033 compatibility shim — include StateTransitionContext.h instead.
 */
#pragma once

#include "bcos-evm/eth/pipeline/StateTransitionContext.h"

namespace bcos::evm
{
using TxPipelineContext [[deprecated("use StateTransitionContext (ADR-033)")]] =
    StateTransitionContext;
using TxPipelineInputs [[deprecated("use StateTransitionInputs (ADR-033)")]] =
    StateTransitionInputs;
using TxPipelineExitKind [[deprecated("use StateTransitionExitKind (ADR-033)")]] =
    StateTransitionExitKind;
}  // namespace bcos::evm
