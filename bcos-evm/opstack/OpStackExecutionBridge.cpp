#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/eth/orchestration/TxPipeline.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-evm/opstack/OpStackOrchestrationErrorPolicy.h"
#include "bcos-evm/opstack/OpStackPipelineHookBinder.h"
#include "bcos-evm/opstack/OpStackPipelineInternals.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackTxPrecheck.h"
#include "bcos-evm/opstack/OpStackVmHostPolicy.h"
#include "bcos-evm/opstack/fee/OpStackFee.h"
#include <algorithm>
#include <stdexcept>

namespace bcos::evm
{

task::Task<OpStackExecutionResult> opStackExecute(OpStackExecutionRequest input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("opStackExecute requires stateView/vm/hashImpl");
    }

    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("opstack", input.blockInfo.number, input.txHash));

    OpStackExecutionResult output;
    TxPipelineContext ctx{*input.stateView, input.message, input.revisionConfig, bcos::u256(0)};
    ctx.txProps = input.txProps;
    ctx.inputs.vm = input.vm;
    ctx.inputs.hashImpl = input.hashImpl;
    ctx.inputs.blockInfo = input.blockInfo;
    ctx.inputs.blockHashes = input.blockHashes;
    ctx.inputs.accessList = input.accessList;
    ctx.inputs.authorizationListPresent = input.authorizationListPresent;
    ctx.inputs.authorizations = input.authorizations;
    ctx.inputs.web3TypedTxKind = input.web3TypedTxKind;

    OpStackVmHostPolicy opHostExtension(&ctx.state, input.blockInfo.baseFee);
    ctx.extension = &opHostExtension;

    auto const feeParams = loadOpStackFeeParams(ctx.state);
    input.opTxExecutor.m_l1CostFunc = wireL1CostFuncWithState(input.forkSchedule, ctx.state);
    input.opTxExecutor.m_operatorCostFunc =
        wireOperatorCostFuncWithState(input.forkSchedule, ctx.state);

    OpStackFeeContext feeCtx;
    feeCtx.m_call = input.call;
    feeCtx.m_isDepositTx = isDepositTx(input);
    feeCtx.m_gasTipCap = input.gasTipCap;
    feeCtx.m_gasFeeCap = input.gasFeeCap;
    feeCtx.m_hasGasFeeCap = true;
    feeCtx.m_effectiveGasPrice =
        resolveEffectiveGasPrice(input.gasTipCap, input.gasFeeCap, input.blockInfo.baseFee);
    feeCtx.m_blockInfo = input.blockInfo;
    feeCtx.m_skipNonceChecks = input.skipNonceChecks;
    feeCtx.m_skipTransactionChecks = input.skipTransactionChecks;
    feeCtx.m_noBaseFee = input.noBaseFee;
    feeCtx.m_floorDataGas = input.floorDataGas;
    feeCtx.m_accessList = input.accessList;
    feeCtx.m_web3TypedTxKind = input.web3TypedTxKind;
    feeCtx.m_authTupleCount = static_cast<uint64_t>(input.authorizations.size());
    feeCtx.m_blobGasFeeCap = input.blobGasFeeCap;
    feeCtx.m_blobVersionedHashes = input.blobVersionedHashes;
    feeCtx.m_rollupCostData = input.rollupCostData;

    trace::logMessageContext(input.message);

    if (auto preCheckError = opStackTxPrecheck(input, ctx.state); preCheckError.has_value())
    {
        output.evmcResult = std::move(*preCheckError);
        co_return output;
    }

    GasPoolHooks gasPool{
        .subGas = input.gasPoolSubGasHook,
        .returnGas = input.gasPoolReturnGasHook,
    };

    if (feeCtx.m_isDepositTx)
    {
        output.receiptMeta.depositNonce = ctx.state.get_nonce(input.message.sender);
        if (input.depositTx.has_value() && input.depositTx->mint.has_value() &&
            *input.depositTx->mint > 0)
        {
            ctx.state.set_balance(input.message.sender,
                ctx.state.get_balance(input.message.sender) + *input.depositTx->mint);
        }

        ctx.state.checkpoint();

        OpStackPipelineHookBinder::HookBindingContext session{input, feeCtx};
        auto hooks = OpStackPipelineHookBinder::buildHooks(session);
        OpStackOrchestrationErrorPolicy errorPolicy;
        runTxPipeline(ctx, hooks, errorPolicy);

        output.evmcResult = std::move(ctx.evmcResult);
        output.logs = std::move(ctx.kernelOutput.logs);

        auto settled =
            co_await settleDeposit(ctx, ctx.exitKind, output.evmcResult.status_code, gasPool);

        EVM_LOG(DEBUG) << LOG_DESC("opStackExecute deposit done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                       << LOG_KV("gasUsed", settled.gasUsed);

        output.gasUsed = settled.gasUsed;
        output.stateDiff = ctx.state.build_diff();
        co_return output;
    }

    if (gasPool.subGas)
    {
        auto const gasLimitForPool =
            static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
        if (!gasPool.subGas(gasLimitForPool))
        {
            output.evmcResult = makeOutOfGasLimitResult();
            co_return output;
        }
    }

    auto buyGasOk = co_await input.opTxExecutor.buyGas(ctx, feeCtx);
    if (!buyGasOk)
    {
        if (gasPool.returnGas)
        {
            auto const gasLimitForPool =
                static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
            gasPool.returnGas(gasLimitForPool, 0);
        }
        output.evmcResult = std::move(*feeCtx.m_evmcResult);
        co_return output;
    }

    ctx.gasPrice = feeCtx.m_effectiveGasPrice;

    OpStackPipelineHookBinder::HookBindingContext session{input, feeCtx};
    auto hooks = OpStackPipelineHookBinder::buildHooks(session);
    OpStackOrchestrationErrorPolicy errorPolicy;
    runTxPipeline(ctx, hooks, errorPolicy);

    output.evmcResult = std::move(ctx.evmcResult);
    output.logs = std::move(ctx.kernelOutput.logs);

    auto settled = co_await settleNormal(ctx, feeCtx, ctx.exitKind, input.opTxExecutor, gasPool);

    EVM_LOG(DEBUG) << LOG_DESC("opStackExecute done")
                   << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                   << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                   << LOG_KV("gasUsed", settled.gasUsed) << LOG_KV("l1Fee", feeCtx.m_l1CostCharged);

    output.gasUsed = settled.gasUsed;
    output.receiptMeta.l1Fee = feeCtx.m_l1CostCharged;
    if (isOpStackIsthmus(input.forkSchedule, feeCtx.m_blockInfo.timestamp) &&
        input.opTxExecutor.m_operatorCostFunc)
    {
        auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, settled.gasUsed));
        output.receiptMeta.operatorFee =
            input.opTxExecutor.m_operatorCostFunc(gasUsed, feeCtx.m_blockInfo.timestamp);
        if (feeParams.operatorFeeScalar != 0 || feeParams.operatorFeeConstant != 0)
        {
            output.receiptMeta.operatorFeeScalar = feeParams.operatorFeeScalar;
            output.receiptMeta.operatorFeeConstant = feeParams.operatorFeeConstant;
        }
    }
    output.stateDiff = ctx.state.build_diff();
    co_return output;
}
}  // namespace bcos::evm
