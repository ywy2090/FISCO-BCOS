/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Canonical stateTransitionExecute pipeline implementation.
 * @file StateTransitionExecute.cpp
 *
 * Fixed transaction-level flow shared by Eth / Fisco / OpStack apply paths.
 * Chain policy is injected via StateTransitionHooks; failure mapping via
 * StateTransitionErrorPolicy. See StateTransitionExecute.h for step order.
 */

#include "bcos-evm/eth/kernel/state-transition/StateTransitionExecute.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <stdexcept>

namespace bcos::evm
{
namespace
{
/// EIP-7623 settlement needs pre-EVM calldata gas components for refund math.
void captureSettlementSnapshot(StateTransitionContext& ctx)
{
    if (ctx.intrinsicGasMode != IntrinsicGasMode::FloorDataGas)
    {
        return;
    }

    ctx.snapshot.eip7623SnapshotActive = true;
    ctx.snapshot.calldata =
        gas::calcEip7623Components(bytesConstRef(ctx.message.input_data, ctx.message.input_size));
    ctx.snapshot.evmGasRefund = ctx.evmGasRefund;
}
}  // namespace

void stateTransitionExecute(StateTransitionContext& ctx, StateTransitionHooks const& hooks,
    StateTransitionErrorPolicy const& errorPolicy)
{
    // onComplete runs on every exit path (early-exit, success, exception).
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
    ctx.intrinsicGasMode = intrinsicPolicy.mode;
    ctx.gasAccounting = {};

    EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute begin")
                   << LOG_KV("kind", trace::callKind(ctx.message.kind))
                   << LOG_KV("depth", ctx.message.depth) << LOG_KV("gas", ctx.message.gas)
                   << LOG_KV("originalGas", ctx.originalGasLimit)
                   << LOG_KV("sender", trace::evmcAddress(ctx.message.sender))
                   << LOG_KV("recipient", trace::evmcAddress(ctx.message.recipient))
                   << LOG_KV("intrinsicMode", trace::intrinsicGasMode(intrinsicPolicy.mode))
                   << LOG_KV("revision", static_cast<int>(ctx.revisionConfig.revision));

    try
    {
        // --- Phase 1: message normalization + entry rules ---
        hooks.onNormalizeMessage(ctx);
        EVM_LOG(TRACE) << LOG_DESC("stateTransitionExecute step")
                       << LOG_KV("step", "onNormalizeMessage") << LOG_KV("gas", ctx.message.gas);

        hooks.onPreCheckRules(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = StateTransitionExitKind::RulesRejected;
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        hooks.onPreCheckGasAffordable(ctx);
        if (ctx.earlyExit)
        {
            ctx.exitKind = StateTransitionExitKind::GasAffordRejected;
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute early-exit")
                           << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                           << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code));
            return;
        }

        // --- Phase 2: kernel intrinsic gas debit (mode from hooks) ---
        ctx.gasAccounting.gasBeforeIntrinsicDebit = ctx.message.gas;
        ctx.gasAccounting.intrinsicDebitAttempted = true;
        auto const debitOutcome = deductIntrinsicGas(ctx.message, intrinsicPolicy);
        if (!debitOutcome.ok)
        {
            ctx.earlyExit = true;
            ctx.exitKind = StateTransitionExitKind::IntrinsicRejected;
            EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute intrinsic rejected")
                           << LOG_KV("failure", trace::intrinsicGasFailure(debitOutcome.failure))
                           << LOG_KV("gasBefore", ctx.gasAccounting.gasBeforeIntrinsicDebit)
                           << LOG_KV("gasLeft", debitOutcome.gasLeftOnFailure);
            errorPolicy.onIntrinsicGasFailure(ctx, debitOutcome.failure);
            return;
        }
        ctx.gasAccounting.intrinsicDebitSucceeded = true;
        ctx.gasAccounting.intrinsicGasDebited = debitOutcome.debitAmount;
        ctx.gasAccounting.gasAfterIntrinsicDebit = ctx.message.gas;
        if (debitOutcome.debitAmount > 0)
        {
            EVM_LOG(TRACE) << LOG_DESC("stateTransitionExecute intrinsic debit")
                           << LOG_KV("debit", debitOutcome.debitAmount)
                           << LOG_KV("gasBefore", ctx.gasAccounting.gasBeforeIntrinsicDebit)
                           << LOG_KV("gasAfter", ctx.gasAccounting.gasAfterIntrinsicDebit);
        }

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

        // --- Phase 3: EVM entry via innerExecute ---
        EVM_LOG(TRACE) << LOG_DESC("stateTransitionExecute step")
                       << LOG_KV("step", "onInvokeInnerExecute") << LOG_KV("gas", ctx.message.gas);

        auto executeInput = ctx.toInnerExecuteInput();
        hooks.onTuneInnerExecuteInput(executeInput);

        ctx.gasAccounting.gasAtEvmEntry = executeInput.message.gas;
        ctx.gasAccounting.reachedEvmEntry = true;

        auto kernelOutput = hooks.onInvokeInnerExecute(std::move(executeInput));
        ctx.evmcResult = adoptEvmcResult(std::move(kernelOutput.result), *ctx.inputs.hashImpl);
        ctx.stateDiff = std::move(kernelOutput.stateDiff);
        ctx.logs = std::move(kernelOutput.logs);
        ctx.evmGasRefund = kernelOutput.gasRefund;

        captureSettlementSnapshot(ctx);

        // --- Phase 4: post-EVM normalization (included-vmerr, receipt fields) ---
        errorPolicy.onFinalizeGasUsed(ctx);
        ctx.exitKind = StateTransitionExitKind::Completed;

        EVM_LOG(DEBUG) << LOG_DESC("stateTransitionExecute done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                       << LOG_KV("gasLeft", ctx.evmcResult.gas_left)
                       << LOG_KV("gasRefund", ctx.evmGasRefund)
                       << LOG_KV("logCount", ctx.logs.size());
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
