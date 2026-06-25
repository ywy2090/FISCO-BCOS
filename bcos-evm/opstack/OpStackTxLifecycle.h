#pragma once

#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{

/// ADR-023 deep module: precheck → deposit|normal branch → pipeline → settle → receipt.
task::Task<OpStackExecutionResult> runOpStackTxLifecycle(OpStackExecutionRequest input);

}  // namespace bcos::evm
