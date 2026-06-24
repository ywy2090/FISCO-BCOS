#pragma once

#include "bcos-evm/bcos/FiscoExecutionBridge.h"

namespace bcos::evm
{
using ExecuteViaHostInput [[deprecated("use FiscoExecutionRequest")]] = FiscoExecutionRequest;
using ExecuteViaHostOutput [[deprecated("use FiscoExecutionResult")]] = FiscoExecutionResult;

[[deprecated("use fiscoExecute")]] inline task::Task<FiscoExecutionResult> executeViaHost(
    FiscoExecutionRequest input)
{
    return fiscoExecute(std::move(input));
}
}  // namespace bcos::evm
