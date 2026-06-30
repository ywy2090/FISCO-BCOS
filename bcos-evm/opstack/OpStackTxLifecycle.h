#pragma once

#include "bcos-evm/opstack/OpStackExecute.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{

/// deep module: precheck → deposit|normal branch → pipeline → settle → receipt.
task::Task<OpStackExecutionResult> runOpStackTxLifecycle(OpStackExecutionRequest input);

}  // namespace bcos::evm
