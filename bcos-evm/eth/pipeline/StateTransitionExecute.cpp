#include "bcos-evm/eth/pipeline/StateTransitionExecute.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/pipeline/AdoptEvmcResult.h"
#include "bcos-evm/eth/pipeline/CaptureSettlementSnapshot.h"
#include "bcos-evm/eth/pipeline/EvmTxContextView.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <stdexcept>

namespace bcos::evm
{

// geth: stateTransition.execute — ADR-030 / ADR-031 canonical
void stateTransitionExecute(StateTransitionContext& ctx, ChainPrecheckPolicy const& precheckPolicy,
    OrchestrationErrorPolicy const& errorPolicy)
{
    struct PipelineCompleteGuard
    {
        StateTransitionContext& ctx;
        OrchestrationErrorPolicy const& errorPolicy;
        ~PipelineCompleteGuard() { errorPolicy.onPipelineComplete(ctx); }
    } completeGuard{ctx, errorPolicy};

    if (ctx.inputs.vm == nullptr || ctx.inputs.hashImpl == nullptr)
    {
        throw std::invalid_argument("stateTransitionExecute requires vm/hashImpl");
    }

    auto const intrinsicPolicy = precheckPolicy.deductIntrinsicGasParams();

    ctx.earlyExit = false;
    ctx.exitKind = StateTransitionExitKind::None;
    ctx.intrinsicDebitMode = intrinsicPolicy.mode;

    EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute begin")
                   << LOG_KV("kind", trace::callKind(ctx.message.kind))
                   << LOG_KV("depth", ctx.message.depth) << LOG_KV("gas", ctx.message.gas)
                   << LOG_KV("originalGas", ctx.originalGasLimit)
                   << LOG_KV("sender", trace::evmcAddress(ctx.message.sender))
                   << LOG_KV("recipient", trace::evmcAddress(ctx.message.recipient))
                   << LOG_KV("intrinsicMode", trace::intrinsicDebitMode(intrinsicPolicy.mode))
                   << LOG_KV("revision", static_cast<int>(ctx.revisionConfig.revision));

    try
    {
        // geth: preCheck (pipelineSetupMessage) — ADR-030
        precheckPolicy.pipelineSetupMessage(ctx);
        EVM_LOG(TRACE) << LOG_DESC("stateTransitionExecute step")
                       << LOG_KV("step", "pipelineSetupMessage") << LOG_KV("gas", ctx.message.gas);

        // geth: preCheck (rules) — ADR-030
        precheckPolicy.pipelineCheckRules(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = StateTransitionExitKind::RulesRejected;
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        // geth: preCheck (buyGas / gas affordable) — ADR-030
        precheckPolicy.pipelineCheckGasAffordable(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = StateTransitionExitKind::GasAffordRejected;
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        // geth: IntrinsicGas — ADR-030
        auto const gasBeforeDebit = ctx.message.gas;
        auto const debitOutcome = deductIntrinsicGas(ctx.message, intrinsicPolicy);
        if (!debitOutcome.ok)
        {
            ctx.earlyExit = true;
            ctx.exitKind = StateTransitionExitKind::IntrinsicRejected;
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute intrinsic rejected")
                           << LOG_KV("failure", trace::intrinsicDebitFailure(debitOutcome.failure))
                           << LOG_KV("gasBefore", gasBeforeDebit)
                           << LOG_KV("gasLeft", debitOutcome.gasLeftOnFailure);
            errorPolicy.onIntrinsicGasFailure(ctx, debitOutcome.failure);
            return;
        }
        if (debitOutcome.debitAmount > 0)
        {
            EVM_LOG(TRACE) << LOG_DESC("stateTransitionExecute intrinsic debit")
                           << LOG_KV("debit", debitOutcome.debitAmount)
                           << LOG_KV("gasBefore", gasBeforeDebit)
                           << LOG_KV("gasAfter", ctx.message.gas);
        }

        // geth: CanTransfer — ADR-030
        precheckPolicy.pipelineCheckBalance(ctx);
        if (ctx.earlyExit)
        {
            if (ctx.exitKind == StateTransitionExitKind::None)
            {
                ctx.exitKind = StateTransitionExitKind::GasAffordRejected;
            }
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                           << LOG_KV("gas", ctx.message.gas);
            return;
        }

        // geth: innerExecute — ADR-030
        EVM_LOG(TRACE) << LOG_DESC("stateTransitionExecute step")
                       << LOG_KV("step", "pipelineInvokeEvmKernel")
                       << LOG_KV("gas", ctx.message.gas);

        if (ctx.txContextView == nullptr)
        {
            throw std::invalid_argument("stateTransitionExecute requires wired EvmTxContextView");
        }
        auto executeInput = ctx.txContextView->toInnerExecuteInput(ctx);
        precheckPolicy.pipelineTuneKernelInput(executeInput);

        ctx.kernelOutput = precheckPolicy.pipelineInvokeEvmKernel(std::move(executeInput));
        ctx.evmcResult = adoptEvmcResult(std::move(ctx.kernelOutput.result), *ctx.inputs.hashImpl);

        captureSettlementSnapshot(ctx, ctx.kernelOutput);

        errorPolicy.onPostExecuteNormalize(ctx);
        ctx.exitKind = StateTransitionExitKind::Completed;

        EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left)
                       << LOG_KV("gasRefund", ctx.kernelOutput.gasRefund)
                       << LOG_KV("logCount", ctx.kernelOutput.logs.size());
    }
    catch (...)
    {
        ctx.exitKind = StateTransitionExitKind::ExceptionHandled;
        EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute exception")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind));
        errorPolicy.onPipelineException(ctx, std::current_exception());
        EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute mapped")
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left);
    }
}

}  // namespace bcos::evm
