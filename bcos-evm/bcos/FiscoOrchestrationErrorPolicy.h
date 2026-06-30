#pragma once

#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoPipelineInternals.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/pipeline/OrchestrationErrorPolicy.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <evmc/evmc.h>
#include <cstring>
#include <string>

namespace bcos::evm
{

struct FiscoOrchestrationErrorPolicy : OrchestrationErrorPolicy
{
    bcos::crypto::Hash const* hashImpl{nullptr};
    bool fixErrorHandling{false};
    bool fixRevertLogs{false};

    void onIntrinsicGasFailure(
        StateTransitionContext& ctx, IntrinsicDebitFailure failure) const override
    {
        std::string reason = "EIP-7623 intrinsic OOG";
        switch (failure)
        {
        case IntrinsicDebitFailure::GasLimitMinimum:
            reason = "EIP-7623 gas limit minimum";
            break;
        case IntrinsicDebitFailure::CalldataOutOfGas:
            reason = "EIP-7623 calldata OOG";
            break;
        case IntrinsicDebitFailure::AuthTupleOutOfGas:
            reason = "EIP-7702 auth tuple OOG";
            break;
        default:
            break;
        }
        ctx.evmcResult = makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::OutOfGas,
            EVMC_OUT_OF_GAS, fixErrorHandling ? 0 : ctx.message.gas, reason, fixErrorHandling);
    }

    void onPipelineException(
        StateTransitionContext& ctx, std::exception_ptr exceptionPtr) const override
    {
        try
        {
            std::rethrow_exception(exceptionPtr);
        }
        catch (protocol::OutOfGas& e)
        {
            ctx.evmcResult = makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::OutOfGas,
                EVMC_OUT_OF_GAS, 0, e.what(), fixErrorHandling);
        }
        catch (protocol::NotEnoughCashError& e)
        {
            ctx.evmcResult = makeErrorEVMCResult(*hashImpl,
                protocol::TransactionStatus::NotEnoughCash, EVMC_INSUFFICIENT_BALANCE,
                fixErrorHandling ? 0 : ctx.message.gas, e.what(), fixErrorHandling);
        }
        catch (NotFoundCodeError&)
        {
            if ((ctx.message.flags & EVMC_STATIC) != 0 || ctx.message.kind == EVMC_DELEGATECALL)
            {
                ctx.evmcResult = makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::None,
                    EVMC_SUCCESS, ctx.message.gas, "", false);
            }
            else
            {
                ctx.evmcResult =
                    makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::RevertInstruction,
                        EVMC_REVERT, ctx.message.gas, "Call address error.", fixErrorHandling);
            }
        }
        catch (std::exception&)
        {
            ctx.evmcResult = makeErrorEVMCResult(*hashImpl,
                fixErrorHandling ? protocol::TransactionStatus::Unknown :
                                   protocol::TransactionStatus::OutOfGas,
                EVMC_INTERNAL_ERROR, fixErrorHandling ? 0 : ctx.message.gas, "", fixErrorHandling);
        }

        if (ctx.state.has_checkpoint())
        {
            ctx.state.revert();
        }
    }

    void onPostExecuteNormalize(StateTransitionContext& ctx) const override
    {
        if ((ctx.message.kind == EVMC_CREATE || ctx.message.kind == EVMC_CREATE2) &&
            ctx.evmcResult.status_code == EVMC_SUCCESS &&
            std::memcmp(ctx.evmcResult.create_address.bytes, EMPTY_EVM_ADDRESS.bytes,
                sizeof(ctx.evmcResult.create_address.bytes)) == 0)
        {
            ctx.evmcResult.create_address = ctx.message.recipient;
        }

        if (fixRevertLogs && ctx.evmcResult.status_code != EVMC_SUCCESS)
        {
            ctx.kernelOutput.logs.clear();
        }
    }

    void onPipelineComplete(StateTransitionContext& ctx) const override
    {
        if (ctx.evmcResult.gas_left < 0)
        {
            ctx.evmcResult = makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::OutOfGas,
                EVMC_OUT_OF_GAS, fixErrorHandling ? 0 : ctx.message.gas, "", fixErrorHandling);
        }
    }
};

}  // namespace bcos::evm
