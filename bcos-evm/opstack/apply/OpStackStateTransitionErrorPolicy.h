#pragma once

#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/eth/kernel/state-transition/IncludedTxVmerrNormalize.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionErrorPolicy.h"
#include "bcos-evm/opstack/apply/OpStackEvmResult.h"

namespace bcos::evm
{

struct OpStackStateTransitionErrorPolicy : StateTransitionErrorPolicy
{
    void onIntrinsicGasFailure(
        StateTransitionContext& ctx, IntrinsicGasFailure /*failure*/) const override
    {
        ctx.evmcResult = makeOutOfGasLimitResult();
    }

    void onException(
        StateTransitionContext& ctx, std::exception_ptr /*exceptionPtr*/) const override
    {
        ctx.evmcResult = makeInternalErrorResult();

        if (ctx.state.has_checkpoint())
        {
            ctx.state.revert();
        }
    }

    /// Included top-level vmerr settlement (ADR-015 state semantics).
    /// Does not apply normalizeSetCodeTransactionVmerr — OpStack keeps failed receipt for 7702
    /// REVERT per op-geth parity (see opstack-vs-op-geth-parity-validation D3).
    void onFinalizeGasUsed(StateTransitionContext& ctx) const override
    {
        if (ctx.inputs.web3TypedTxKind == toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit))
        {
            return;
        }
        // Op-geth D3: keep raw top-level REVERT status_code; Eth reference normalizes via ADR-015.
        if (ctx.evmcResult.status_code == EVMC_REVERT)
        {
            ctx.topLevelIncludedTxVmError = false;
            return;
        }
        ctx.topLevelIncludedTxVmError =
            isTopLevelIncludedTxVmError(ctx.evmcResult.status_code, ctx.message.depth);
        normalizeIncludedTxVmerr(ctx.evmcResult, ctx.message.depth);
    }
};

}  // namespace bcos::evm
