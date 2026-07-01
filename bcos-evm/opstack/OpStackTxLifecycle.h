#pragma once

#include "bcos-evm/opstack/ApplyOpStackMessage.h"
#include <bcos-task/Task.h>

namespace bcos::evm
{

/// deep module: precheck → deposit|normal branch → pipeline → settle → receipt.
task::Task<OpStackMessageResult> runOpStackTxLifecycle(OpStackMessageRequest input);

}  // namespace bcos::evm
