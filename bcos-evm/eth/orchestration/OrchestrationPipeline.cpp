#include "bcos-evm/eth/orchestration/OrchestrationPipeline.h"
#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/orchestration/adoptEvmcResult.h"
#include "bcos-evm/eth/orchestration/buildExecuteMessageInput.h"
#include "bcos-evm/eth/orchestration/captureSettlementSnapshot.h"
#include <stdexcept>

namespace bcos::evm
{

void runOrchestration(OrchestrationContext& ctx, OrchestrationHooks const& hooks)
{
    if (ctx.inputs.vm == nullptr || ctx.inputs.hashImpl == nullptr)
    {
        throw std::invalid_argument("runOrchestration requires vm/hashImpl");
    }

    ctx.earlyExit = false;
    ctx.exitKind = OrchestrationExitKind::None;
    ctx.intrinsicDebitMode = hooks.intrinsicPolicy.mode;

    try
    {
        hooks.prepareMessage(ctx);

        hooks.preExecute(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = OrchestrationExitKind::PreExecuteRejected;
            return;
        }

        hooks.preDebitEntry(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = OrchestrationExitKind::PreDebitRejected;
            return;
        }

        auto const debitOutcome = debitIntrinsicGas(ctx.message, hooks.intrinsicPolicy);
        if (!debitOutcome.ok)
        {
            ctx.earlyExit = true;
            ctx.exitKind = OrchestrationExitKind::IntrinsicRejected;
            hooks.mapIntrinsicFailure(ctx, debitOutcome.failure);
            return;
        }

        hooks.preKernel(ctx);

        auto executeInput = buildExecuteMessageInput(ctx);
        hooks.tuneKernelInput(executeInput);

        ctx.kernelOutput = executeMessage(std::move(executeInput));
        ctx.evmcResult = adoptEvmcResult(std::move(ctx.kernelOutput.result), *ctx.inputs.hashImpl);

        captureSettlementSnapshot(ctx, ctx.kernelOutput);

        hooks.postAdopt(ctx);
        hooks.postSettle(ctx);
        ctx.exitKind = OrchestrationExitKind::KernelCompleted;
    }
    catch (...)
    {
        ctx.exitKind = OrchestrationExitKind::ExceptionMapped;
        hooks.mapException(ctx, std::current_exception());
    }
}

}  // namespace bcos::evm
