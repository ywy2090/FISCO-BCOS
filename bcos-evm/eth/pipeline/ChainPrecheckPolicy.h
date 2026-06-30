#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/pipeline/IntrinsicGasDebit.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"

namespace bcos::evm
{

/// Chain-specific pre-execution checks for runTxPipeline (setup, rules, gas, balance, tuning).
struct ChainPrecheckPolicy
{
    virtual ~ChainPrecheckPolicy() = default;

    virtual IntrinsicGasDebitParams intrinsicGasDebitParams() const = 0;

    virtual void setupMessage(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void checkTransactionRules(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void checkGasAffordable(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void checkBalanceAndValue(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void tuneExecutionInput(ExecuteMessageInput& input) const { (void)input; }

    virtual ExecuteMessageOutput runEvmExecution(ExecuteMessageInput&& input) const
    {
        return executeMessage(std::move(input));
    }

    // geth: preCheck slices — ADR-030 Tier A aliases (forward to ADR-029 canonical names)
    void preCheckRules(TxPipelineContext& ctx) const { checkTransactionRules(ctx); }

    void preCheckGasAffordable(TxPipelineContext& ctx) const { checkGasAffordable(ctx); }

    void preCheckCanTransfer(TxPipelineContext& ctx) const { checkBalanceAndValue(ctx); }

    void normalizeMessage(TxPipelineContext& ctx) const { setupMessage(ctx); }

    [[nodiscard]] ExecuteMessageOutput pipelineInvokeEvmKernel(ExecuteMessageInput&& input) const
    {
        return runEvmExecution(std::move(input));
    }
};

}  // namespace bcos::evm
