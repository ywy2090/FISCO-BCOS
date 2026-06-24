#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/orchestration/OrchestrationContext.h"
#include <exception>
#include <functional>

namespace bcos::evm
{

struct OrchestrationHooks
{
    IntrinsicGasPolicy intrinsicPolicy{};

    std::function<void(OrchestrationContext&)> prepareMessage = [](OrchestrationContext&) {};
    std::function<void(OrchestrationContext&)> preExecute = [](OrchestrationContext&) {};
    std::function<void(OrchestrationContext&)> preDebitEntry = [](OrchestrationContext&) {};
    std::function<void(OrchestrationContext&)> preKernel = [](OrchestrationContext&) {};
    std::function<void(ExecuteMessageInput&)> tuneKernelInput = [](ExecuteMessageInput&) {};
    std::function<void(OrchestrationContext&)> postAdopt = [](OrchestrationContext&) {};
    std::function<void(OrchestrationContext&)> postSettle = [](OrchestrationContext&) {};
    std::function<void(OrchestrationContext&, IntrinsicDebitFailure)> mapIntrinsicFailure =
        [](OrchestrationContext&, IntrinsicDebitFailure) {};
    std::function<void(OrchestrationContext&, std::exception_ptr)> mapException =
        [](OrchestrationContext&, std::exception_ptr) {};
    // Test-only seam: when set, pipeline calls this instead of executeMessage (OpStack spy tests).
    std::function<ExecuteMessageOutput(ExecuteMessageInput&&)> executeMessageOverride;
};

}  // namespace bcos::evm
