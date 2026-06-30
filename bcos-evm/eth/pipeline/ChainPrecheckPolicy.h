#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/pipeline/IntrinsicGasDebit.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"

namespace bcos::evm
{

/// Chain-specific pre-execution checks for stateTransitionExecute (setup, rules, gas, balance,
/// tuning).
struct ChainPrecheckPolicy
{
    virtual ~ChainPrecheckPolicy() = default;

    virtual IntrinsicGasDebitParams intrinsicGasDebitParams() const = 0;

    virtual void pipelineSetupMessage(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void pipelineCheckRules(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void pipelineCheckGasAffordable(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void pipelineCheckBalance(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void pipelineTuneKernelInput(ExecuteMessageInput& input) const { (void)input; }

    virtual ExecuteMessageOutput pipelineInvokeEvmKernel(ExecuteMessageInput&& input) const
    {
        return innerExecute(std::move(input));
    }

    // ADR-029 deprecated aliases (1 release)
    [[deprecated("Use pipelineSetupMessage")]] void setupMessage(TxPipelineContext& ctx) const
    {
        pipelineSetupMessage(ctx);
    }

    [[deprecated("Use pipelineCheckRules")]] void checkTransactionRules(
        TxPipelineContext& ctx) const
    {
        pipelineCheckRules(ctx);
    }

    [[deprecated("Use pipelineCheckGasAffordable")]] void checkGasAffordable(
        TxPipelineContext& ctx) const
    {
        pipelineCheckGasAffordable(ctx);
    }

    [[deprecated("Use pipelineCheckBalance")]] void checkBalanceAndValue(
        TxPipelineContext& ctx) const
    {
        pipelineCheckBalance(ctx);
    }

    [[deprecated("Use pipelineTuneKernelInput")]] void tuneExecutionInput(
        ExecuteMessageInput& input) const
    {
        pipelineTuneKernelInput(input);
    }

    [[deprecated("Use pipelineInvokeEvmKernel")]] ExecuteMessageOutput runEvmExecution(
        ExecuteMessageInput&& input) const
    {
        return pipelineInvokeEvmKernel(std::move(input));
    }

    // geth: preCheck slices — ADR-030 Tier A aliases (forward to ADR-029 canonical names)
    void preCheckRules(TxPipelineContext& ctx) const { pipelineCheckRules(ctx); }

    void preCheckGasAffordable(TxPipelineContext& ctx) const { pipelineCheckGasAffordable(ctx); }

    void preCheckCanTransfer(TxPipelineContext& ctx) const { pipelineCheckBalance(ctx); }

    // geth: TransactionToMessage — ADR-030
    void normalizeMessage(TxPipelineContext& ctx) const { pipelineSetupMessage(ctx); }
};

}  // namespace bcos::evm
