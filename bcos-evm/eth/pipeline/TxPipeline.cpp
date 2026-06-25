#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/pipeline/AdoptEvmcResult.h"
#include "bcos-evm/eth/pipeline/BuildExecuteMessageInput.h"
#include "bcos-evm/eth/pipeline/CaptureSettlementSnapshot.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <stdexcept>

namespace bcos::evm
{

void runTxPipeline(TxPipelineContext& ctx, TxPipelineHooks const& hooks,
    OrchestrationErrorPolicy const& errorPolicy)
{
    struct PipelineCompleteGuard
    {
        TxPipelineContext& ctx;
        OrchestrationErrorPolicy const& errorPolicy;
        ~PipelineCompleteGuard() { errorPolicy.onPipelineComplete(ctx); }
    } completeGuard{ctx, errorPolicy};

    if (ctx.inputs.vm == nullptr || ctx.inputs.hashImpl == nullptr)
    {
        throw std::invalid_argument("runTxPipeline requires vm/hashImpl");
    }

    ctx.earlyExit = false;
    ctx.exitKind = TxPipelineExitKind::None;
    ctx.intrinsicDebitMode = hooks.intrinsicPolicy.mode;

    EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline begin")
                   << LOG_KV("kind", trace::callKind(ctx.message.kind))
                   << LOG_KV("depth", ctx.message.depth) << LOG_KV("gas", ctx.message.gas)
                   << LOG_KV("originalGas", ctx.originalGasLimit)
                   << LOG_KV("sender", trace::evmcAddress(ctx.message.sender))
                   << LOG_KV("recipient", trace::evmcAddress(ctx.message.recipient))
                   << LOG_KV("intrinsicMode", trace::intrinsicDebitMode(hooks.intrinsicPolicy.mode))
                   << LOG_KV("revision", static_cast<int>(ctx.revisionConfig.revision));

    try
    {
        hooks.txSetupMessage(ctx);
        EVM_LOG(TRACE) << LOG_DESC("runTxPipeline step") << LOG_KV("step", "txSetupMessage")
                       << LOG_KV("gas", ctx.message.gas);

        hooks.txCheckTransactionRules(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = TxPipelineExitKind::RulesRejected;
            EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        hooks.txCheckGasAffordable(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = TxPipelineExitKind::GasAffordRejected;
            EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        auto const gasBeforeDebit = ctx.message.gas;
        auto const debitOutcome = debitIntrinsicGas(ctx.message, hooks.intrinsicPolicy);
        if (!debitOutcome.ok)
        {
            ctx.earlyExit = true;
            ctx.exitKind = TxPipelineExitKind::IntrinsicRejected;
            EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline intrinsic rejected")
                           << LOG_KV("failure", trace::intrinsicDebitFailure(debitOutcome.failure))
                           << LOG_KV("gasBefore", gasBeforeDebit)
                           << LOG_KV("gasLeft", debitOutcome.gasLeftOnFailure);
            errorPolicy.onIntrinsicGasFailure(ctx, debitOutcome.failure);
            return;
        }
        if (debitOutcome.debitAmount > 0)
        {
            EVM_LOG(TRACE) << LOG_DESC("runTxPipeline intrinsic debit")
                           << LOG_KV("debit", debitOutcome.debitAmount)
                           << LOG_KV("gasBefore", gasBeforeDebit)
                           << LOG_KV("gasAfter", ctx.message.gas);
        }

        hooks.txCheckBalanceAndValue(ctx);
        if (ctx.earlyExit)
        {
            if (ctx.exitKind == TxPipelineExitKind::None)
            {
                ctx.exitKind = TxPipelineExitKind::GasAffordRejected;
            }
            EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                           << LOG_KV("gas", ctx.message.gas);
            return;
        }

        EVM_LOG(TRACE) << LOG_DESC("runTxPipeline step") << LOG_KV("step", "txRunEvmExecution")
                       << LOG_KV("gas", ctx.message.gas);

        auto executeInput = buildExecuteMessageInput(ctx);
        hooks.txTuneExecutionInput(executeInput);

        if (hooks.txRunEvmExecutionOverride)
        {
            ctx.kernelOutput = hooks.txRunEvmExecutionOverride(std::move(executeInput));
        }
        else
        {
            ctx.kernelOutput = executeMessage(std::move(executeInput));
        }
        ctx.evmcResult = adoptEvmcResult(std::move(ctx.kernelOutput.result), *ctx.inputs.hashImpl);

        captureSettlementSnapshot(ctx, ctx.kernelOutput);

        errorPolicy.onPostExecuteNormalize(ctx);
        ctx.exitKind = TxPipelineExitKind::Completed;

        EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left)
                       << LOG_KV("gasRefund", ctx.kernelOutput.gasRefund)
                       << LOG_KV("logCount", ctx.kernelOutput.logs.size());
    }
    catch (...)
    {
        ctx.exitKind = TxPipelineExitKind::ExceptionHandled;
        EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline exception")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind));
        errorPolicy.onPipelineException(ctx, std::current_exception());
        EVM_LOG(DEBUG) << LOG_DESC("runTxPipeline mapped")
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left);
    }
}

}  // namespace bcos::evm
