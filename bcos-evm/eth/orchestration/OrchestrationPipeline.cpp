#include "bcos-evm/eth/orchestration/OrchestrationPipeline.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/orchestration/AdoptEvmcResult.h"
#include "bcos-evm/eth/orchestration/BuildExecuteMessageInput.h"
#include "bcos-evm/eth/orchestration/CaptureSettlementSnapshot.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
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

    EVM_LOG(DEBUG) << LOG_DESC("runOrchestration begin")
                   << LOG_KV("kind", trace::callKind(ctx.message.kind))
                   << LOG_KV("depth", ctx.message.depth) << LOG_KV("gas", ctx.message.gas)
                   << LOG_KV("originalGas", ctx.originalGasLimit)
                   << LOG_KV("sender", trace::evmcAddress(ctx.message.sender))
                   << LOG_KV("recipient", trace::evmcAddress(ctx.message.recipient))
                   << LOG_KV("intrinsicMode", trace::intrinsicDebitMode(hooks.intrinsicPolicy.mode))
                   << LOG_KV("revision", static_cast<int>(ctx.revisionConfig.revision));

    try
    {
        hooks.prepareMessage(ctx);
        EVM_LOG(TRACE) << LOG_DESC("runOrchestration step") << LOG_KV("step", "prepareMessage")
                       << LOG_KV("gas", ctx.message.gas);

        hooks.preExecute(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = OrchestrationExitKind::PreExecuteRejected;
            EVM_LOG(DEBUG) << LOG_DESC("runOrchestration early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        hooks.preDebitEntry(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = OrchestrationExitKind::PreDebitRejected;
            EVM_LOG(DEBUG) << LOG_DESC("runOrchestration early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        auto const gasBeforeDebit = ctx.message.gas;
        auto const debitOutcome = debitIntrinsicGas(ctx.message, hooks.intrinsicPolicy);
        if (!debitOutcome.ok)
        {
            ctx.earlyExit = true;
            ctx.exitKind = OrchestrationExitKind::IntrinsicRejected;
            EVM_LOG(DEBUG) << LOG_DESC("runOrchestration intrinsic rejected")
                           << LOG_KV("failure", trace::intrinsicDebitFailure(debitOutcome.failure))
                           << LOG_KV("gasBefore", gasBeforeDebit)
                           << LOG_KV("gasLeft", debitOutcome.gasLeftOnFailure);
            hooks.mapIntrinsicFailure(ctx, debitOutcome.failure);
            return;
        }
        if (debitOutcome.debitAmount > 0)
        {
            EVM_LOG(TRACE) << LOG_DESC("runOrchestration intrinsic debit")
                           << LOG_KV("debit", debitOutcome.debitAmount)
                           << LOG_KV("gasBefore", gasBeforeDebit)
                           << LOG_KV("gasAfter", ctx.message.gas);
        }

        hooks.preKernel(ctx);
        if (ctx.earlyExit)
        {
            if (ctx.exitKind == OrchestrationExitKind::None)
            {
                ctx.exitKind = OrchestrationExitKind::PreDebitRejected;
            }
            EVM_LOG(DEBUG) << LOG_DESC("runOrchestration early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                           << LOG_KV("gas", ctx.message.gas);
            return;
        }

        EVM_LOG(TRACE) << LOG_DESC("runOrchestration step") << LOG_KV("step", "executeMessage")
                       << LOG_KV("gas", ctx.message.gas);

        auto executeInput = buildExecuteMessageInput(ctx);
        hooks.tuneKernelInput(executeInput);

        if (hooks.executeMessageOverride)
        {
            ctx.kernelOutput = hooks.executeMessageOverride(std::move(executeInput));
        }
        else
        {
            ctx.kernelOutput = executeMessage(std::move(executeInput));
        }
        ctx.evmcResult = adoptEvmcResult(std::move(ctx.kernelOutput.result), *ctx.inputs.hashImpl);

        captureSettlementSnapshot(ctx, ctx.kernelOutput);

        hooks.postAdopt(ctx);
        hooks.postSettle(ctx);
        ctx.exitKind = OrchestrationExitKind::KernelCompleted;

        EVM_LOG(DEBUG) << LOG_DESC("runOrchestration done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left)
                       << LOG_KV("gasRefund", ctx.kernelOutput.gasRefund)
                       << LOG_KV("logCount", ctx.kernelOutput.logs.size());
    }
    catch (...)
    {
        ctx.exitKind = OrchestrationExitKind::ExceptionMapped;
        EVM_LOG(DEBUG) << LOG_DESC("runOrchestration exception")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind));
        hooks.mapException(ctx, std::current_exception());
        EVM_LOG(DEBUG) << LOG_DESC("runOrchestration mapped")
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left);
    }
}

}  // namespace bcos::evm
