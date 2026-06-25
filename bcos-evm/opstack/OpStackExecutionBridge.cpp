#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackTxLifecycle.h"
#include <stdexcept>

namespace bcos::evm
{

task::Task<OpStackExecutionResult> opStackExecute(OpStackExecutionRequest input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("opStackExecute requires stateView/vm/hashImpl");
    }

    co_return co_await runOpStackTxLifecycle(std::move(input));
}

}  // namespace bcos::evm
