#pragma once

#include "bcos-evm/opstack/OpStackExecutionBridge.h"

namespace bcos::evm
{
using OpStackExecuteViaHostInput [[deprecated("use OpStackExecutionRequest")]] =
    OpStackExecutionRequest;
using OpStackExecuteViaHostOutput [[deprecated("use OpStackExecutionResult")]] =
    OpStackExecutionResult;

[[deprecated("use opStackExecute")]] inline task::Task<OpStackExecutionResult>
opStackExecuteViaHost(OpStackExecutionRequest input)
{
    return opStackExecute(std::move(input));
}
}  // namespace bcos::evm
