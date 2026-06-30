#include "bcos-evm/eth/pipeline/StateTransitionExecute.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <stdexcept>

namespace bcos::evm
{
namespace
{
void captureSettlementSnapshot(StateTransitionContext& ctx, InnerExecuteOutput const& kernelOutput)
{
    if (ctx.intrinsicDebitMode != IntrinsicDebitMode::Eip7623)
    {
        return;
    }

    ctx.snapshot.gasLimit = ctx.originalGasLimit;
    ctx.snapshot.calldata =
        gas::calcEip7623Components(bytesConstRef(ctx.message.input_data, ctx.message.input_size));
    ctx.snapshot.evmGasRefund = kernelOutput.gasRefund;
}
}  // namespace

// geth: stateTransition.execute — ADR-030 / ADR-031 canonical
void stateTransitionExecute(StateTransitionContext& ctx, StateTransitionHooks const& hooks,
    StateTransitionErrorPolicy const& errorPolicy)
{
    struct PipelineCompleteGuard
    {
        StateTransitionContext& ctx;
        StateTransitionErrorPolicy const& errorPolicy;
        ~PipelineCompleteGuard() { errorPolicy.onComplete(ctx); }
    } completeGuard{ctx, errorPolicy};

    if (ctx.inputs.vm == nullptr || ctx.inputs.hashImpl == nullptr)
    {
        throw std::invalid_argument("stateTransitionExecute requires vm/hashImpl");
    }

    auto const intrinsicPolicy = hooks.getIntrinsicGasParams();

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
        // geth: preCheck (onNormalizeMessage) — ADR-030
        hooks.onNormalizeMessage(ctx);
        EVM_LOG(TRACE) << LOG_DESC("stateTransitionExecute step")
                       << LOG_KV("step", "onNormalizeMessage") << LOG_KV("gas", ctx.message.gas);

        // geth: preCheck (rules) — ADR-030
        hooks.onPreCheckRules(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = StateTransitionExitKind::RulesRejected;
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        // geth: preCheck (buyGas / gas affordable) — ADR-030
        hooks.onPreCheckGasAffordable(ctx);
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
        hooks.onPreCheckCanTransfer(ctx);
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
                       << LOG_KV("step", "onInvokeInnerExecute") << LOG_KV("gas", ctx.message.gas);

        auto executeInput = ctx.toInnerExecuteInput();
        hooks.onTuneInnerExecuteInput(executeInput);

        ctx.kernelOutput = hooks.onInvokeInnerExecute(std::move(executeInput));
        ctx.evmcResult = adoptEvmcResult(std::move(ctx.kernelOutput.result), *ctx.inputs.hashImpl);

        captureSettlementSnapshot(ctx, ctx.kernelOutput);

        errorPolicy.onFinalizeGasUsed(ctx);
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
        errorPolicy.onException(ctx, std::current_exception());
        EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute mapped")
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left);
    }
}

}  // namespace bcos::evm
