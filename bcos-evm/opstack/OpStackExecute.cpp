#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-evm/opstack/OpStackTxLifecycle.h"
#include <stdexcept>

namespace bcos::evm
{

task::Task<OpStackExecutionResult> applyOpStackMessage(OpStackExecutionRequest input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("applyOpStackMessage requires stateView/vm/hashImpl");
    }

    co_return co_await runOpStackTxLifecycle(std::move(input));
}

}  // namespace bcos::evm
