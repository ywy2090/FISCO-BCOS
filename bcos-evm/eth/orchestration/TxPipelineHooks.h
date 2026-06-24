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

    std::function<void(TxPipelineContext&)> txSetupMessage = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> txCheckTransactionRules = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> txCheckGasAffordable = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> txCheckBalanceAndValue = [](TxPipelineContext&) {};
    std::function<void(ExecuteMessageInput&)> txTuneExecutionInput = [](ExecuteMessageInput&) {};
    std::function<void(TxPipelineContext&)> txPatchExecutionResult = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&)> txFinalizeGasSettlement = [](TxPipelineContext&) {};
    std::function<void(TxPipelineContext&, IntrinsicDebitFailure)> txHandleIntrinsicGasFailure =
        [](TxPipelineContext&, IntrinsicDebitFailure) {};
    std::function<void(TxPipelineContext&, std::exception_ptr)> txHandlePipelineException =
        [](TxPipelineContext&, std::exception_ptr) {};
    // Test-only seam: when set, pipeline calls this instead of executeMessage (OpStack spy tests).
    std::function<ExecuteMessageOutput(ExecuteMessageInput&&)> txRunEvmExecutionOverride;
};

}  // namespace bcos::evm
