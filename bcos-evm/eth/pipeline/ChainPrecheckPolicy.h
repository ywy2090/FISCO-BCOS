#pragma once

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/pipeline/DebitIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"

namespace bcos::evm
{

/// Chain-specific pre-execution checks for runTxPipeline (setup, rules, gas, balance, tuning).
struct ChainPrecheckPolicy
{
    virtual ~ChainPrecheckPolicy() = default;

    virtual IntrinsicGasPolicy intrinsicGasPolicy() const = 0;

    virtual void setupMessage(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void checkTransactionRules(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void checkGasAffordable(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void checkBalanceAndValue(TxPipelineContext& ctx) const { (void)ctx; }

    virtual void tuneExecutionInput(ExecuteMessageInput& input) const { (void)input; }

    virtual ExecuteMessageOutput runEvmExecution(ExecuteMessageInput&& input) const
    {
        return executeMessage(std::move(input));
    }
};

}  // namespace bcos::evm
