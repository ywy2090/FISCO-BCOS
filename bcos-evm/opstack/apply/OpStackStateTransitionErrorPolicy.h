#pragma once

#include "bcos-evm/eth/kernel/state-transition/IncludedTxVmerrNormalize.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionErrorPolicy.h"
#include "bcos-evm/opstack/apply/OpStackEvmResult.h"
#include "bcos-framework/executor/OpStackTxType.h"

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
        ctx.topLevelIncludedTxVmError =
            isTopLevelIncludedTxVmError(ctx.evmcResult.status_code, ctx.message.depth);
        // Deposits settle via finalizeDeposit using raw evmc status (op-geth deposit OOG/revert).
        if (ctx.inputs.web3TypedTxKind == bcos::executor::DEPOSIT_TX_TYPE)
        {
            return;
        }
        normalizeIncludedTxVmerr(ctx.evmcResult, ctx.message.depth);
    }
};

}  // namespace bcos::evm
