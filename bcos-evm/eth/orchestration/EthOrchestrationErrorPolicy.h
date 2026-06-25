#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/orchestration/NormalizeIncludedTxVmerr.h"
#include "bcos-evm/eth/orchestration/OrchestrationErrorPolicy.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

struct EthOrchestrationErrorPolicy : OrchestrationErrorPolicy
{
    void onIntrinsicGasFailure(
        TxPipelineContext& ctx, IntrinsicDebitFailure /*failure*/) const override
    {
        evmc_result failResult{};
        failResult.status_code = EVMC_OUT_OF_GAS;
        failResult.gas_left = 0;
        ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
    }

    void onPipelineException(TxPipelineContext& ctx, std::exception_ptr exceptionPtr) const override
    {
        try
        {
            std::rethrow_exception(exceptionPtr);
        }
        catch (protocol::OutOfGas&)
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_OUT_OF_GAS;
            failResult.gas_left = 0;
            ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
        }
        catch (std::exception&)
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_INTERNAL_ERROR;
            failResult.gas_left = 0;
            ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::Unknown);
        }

        if (ctx.state.has_checkpoint())
        {
            ctx.state.revert();
        }
    }

    void onPostExecuteNormalize(TxPipelineContext& ctx) const override
    {
        normalizeSetCodeTransactionVmerr(
            ctx.evmcResult, ctx.message.depth, ctx.inputs.authorizationListPresent);
        ctx.topLevelIncludedTxVmError =
            isTopLevelIncludedTxVmError(ctx.evmcResult.status_code, ctx.message.depth);
        normalizeIncludedTxVmerr(ctx.evmcResult, ctx.message.depth);
    }
};

}  // namespace bcos::evm
