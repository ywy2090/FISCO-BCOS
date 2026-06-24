#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include <exception>
#include <functional>

namespace bcos::evm
{

struct TxPipelineHooks
{
    IntrinsicGasPolicy intrinsicPolicy{};

    std::function<void(TxPipelineContext&)> prepareMessage = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> preExecute = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> preDebitEntry = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> preKernel = [](TxPipelineContext&) {};
    std::function<void(ExecuteMessageInput&)> tuneKernelInput = [](ExecuteMessageInput&) {};
    std::function<void(TxPipelineContext&)> postAdopt = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> postSettle = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&, IntrinsicDebitFailure)> mapIntrinsicFailure =
        [](TxPipelineContext&, IntrinsicDebitFailure) {};
    std::function<void(TxPipelineContext&, std::exception_ptr)> mapException =
        [](TxPipelineContext&, std::exception_ptr) {};
    // Test-only seam: when set, pipeline calls this instead of executeMessage (OpStack spy tests).
    std::function<ExecuteMessageOutput(ExecuteMessageInput&&)> executeMessageOverride;
};

}  // namespace bcos::evm
