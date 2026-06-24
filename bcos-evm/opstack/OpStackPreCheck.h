#pragma once

#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackTxPrecheck.h"

namespace bcos::evm
{
[[deprecated("use opStackTxPrecheck")]] inline std::optional<EVMCResult> opStackTxPrecheck(
    OpStackExecutionRequest const& input, state::State& state)
{
    return opStackTxPrecheck(input, state);
}
}  // namespace bcos::evm
