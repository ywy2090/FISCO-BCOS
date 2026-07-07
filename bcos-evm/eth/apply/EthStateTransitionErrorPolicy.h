#pragma once

#include "bcos-evm/eth/apply/EthEvmResult.h"
#include "bcos-evm/eth/kernel/state-transition/IncludedTxVmerrNormalize.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionErrorPolicy.h"
#include "bcos-framework/protocol/Exceptions.h"

namespace bcos::evm
{

struct EthStateTransitionErrorPolicy : StateTransitionErrorPolicy
{
    bool isCall{false};

    void onIntrinsicGasFailure(
        StateTransitionContext& ctx, IntrinsicGasFailure /*failure*/) const override
    {
        ctx.evmcResult =
            makeEvmcResult(protocol::TransactionStatus::OutOfGasLimit, EVMC_OUT_OF_GAS);
    }

    void onException(StateTransitionContext& ctx, std::exception_ptr exceptionPtr) const override
    {
        try
        {
            std::rethrow_exception(exceptionPtr);
        }
        catch (protocol::OutOfGas&)
        {
            ctx.evmcResult =
                makeEvmcResult(protocol::TransactionStatus::OutOfGasLimit, EVMC_OUT_OF_GAS);
        }
        catch (std::exception&)
        {
            ctx.evmcResult =
                makeEvmcResult(protocol::TransactionStatus::Unknown, EVMC_INTERNAL_ERROR);
        }

        if (ctx.state.has_checkpoint())
        {
            ctx.state.revert();
        }
    }

    void onFinalizeGasUsed(StateTransitionContext& ctx) const override
    {
        if (isCall)
            return;
        normalizeSetCodeTransactionVmerr(
            ctx.evmcResult, ctx.message.depth, ctx.inputs.authorizationListPresent);
        ctx.topLevelIncludedTxVmError =
            isTopLevelIncludedTxVmError(ctx.evmcResult.status_code, ctx.message.depth);
        normalizeIncludedTxVmerr(ctx.evmcResult, ctx.message.depth);
    }
};

}  // namespace bcos::evm
