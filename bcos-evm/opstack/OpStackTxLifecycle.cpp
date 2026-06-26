#include "bcos-evm/opstack/OpStackTxLifecycle.h"

#include "bcos-evm/eth/gas/Eip1559.h"
#include "bcos-evm/eth/pipeline/TxPipeline.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackOrchestrationProfile.h"
#include "bcos-evm/opstack/OpStackPipelineInternals.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/fee/OpStackFee.h"
#include <algorithm>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
bool acquireGasPool(GasPoolHooks const& gasPool, int64_t originalGasLimit)
{
    if (!gasPool.subGas)
    {
        return true;
    }
    auto const gasLimitForPool = static_cast<uint64_t>(std::max<int64_t>(0, originalGasLimit));
    return gasPool.subGas(gasLimitForPool);
}

void populateFeeContext(OpStackFeeContext& feeCtx, OpStackExecutionRequest const& input)
{
    feeCtx.m_call = input.call;
    feeCtx.m_isDepositTx = isDepositTx(input);
    feeCtx.m_gasTipCap = input.gasTipCap;
    feeCtx.m_gasFeeCap = input.gasFeeCap;
    feeCtx.m_hasGasFeeCap = true;
    feeCtx.m_effectiveGasPrice =
        gas::resolveEffectiveGasPrice(input.gasTipCap, input.gasFeeCap, input.blockInfo.baseFee);
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
}
}  // namespace

task::Task<OpStackExecutionResult> runOpStackTxLifecycle(OpStackExecutionRequest input)
{
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

    OpStackChainCallTargetAdapter chainAdapter(
        &ctx.state, input.blockInfo.baseFee, input.forkSchedule, input.blockInfo.timestamp);
    ctx.chainPort = &chainAdapter;

    auto const feeParams = loadOpStackFeeParams(ctx.state);
    input.opTxExecutor.m_l1CostFunc = wireL1CostFuncWithState(input.forkSchedule, ctx.state);
    input.opTxExecutor.m_operatorCostFunc =
        wireOperatorCostFuncWithState(input.forkSchedule, ctx.state);

    OpStackFeeContext feeCtx;
    populateFeeContext(feeCtx, input);

    OpStackOrchestrationProfile::Session session{input, feeCtx};
    auto bindings = OpStackOrchestrationProfile::bind(session);

    trace::logMessageContext(input.message);

    bindings.precheckPolicy.checkEntryRules(ctx);
    if (ctx.earlyExit)
    {
        output.evmcResult = std::move(ctx.evmcResult);
        co_return output;
    }

    auto gasPool = GasPoolHooks{
        .subGas = input.gasPoolSubGasHook,
        .returnGas = input.gasPoolReturnGasHook,
    };

    if (feeCtx.m_isDepositTx)
    {
        if (!acquireGasPool(gasPool, ctx.originalGasLimit))
        {
            output.evmcResult = makeOutOfGasLimitResult();
            co_return output;
        }

        output.receiptMeta.depositNonce = ctx.state.get_nonce(input.message.sender);
        if (input.depositTx.has_value() && input.depositTx->mint.has_value() &&
            *input.depositTx->mint > 0)
        {
            ctx.state.set_balance(input.message.sender,
                ctx.state.get_balance(input.message.sender) + *input.depositTx->mint);
        }

        ctx.state.checkpoint();

        runTxPipeline(ctx, bindings.precheckPolicy, bindings.errorPolicy);

        output.evmcResult = std::move(ctx.evmcResult);
        output.logs = std::move(ctx.kernelOutput.logs);

        auto settled =
            co_await settleDeposit(ctx, ctx.exitKind, output.evmcResult.status_code, gasPool);

        EVM_LOG(DEBUG) << LOG_DESC("opStackTxLifecycle deposit done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                       << LOG_KV("gasUsed", settled.gasUsed);

        output.gasUsed = settled.gasUsed;
        output.stateDiff = ctx.state.build_diff();
        co_return output;
    }

    if (!acquireGasPool(gasPool, ctx.originalGasLimit))
    {
        output.evmcResult = makeOutOfGasLimitResult();
        co_return output;
    }

    ctx.state.checkpoint();

    auto buyGasOk = co_await input.opTxExecutor.buyGas(ctx, feeCtx);
    if (!buyGasOk)
    {
        abortNormalAfterBuyGas(ctx, gasPool, output, ctx.originalGasLimit);
        output.evmcResult = std::move(ctx.evmcResult);
        co_return output;
    }

    ctx.gasPrice = feeCtx.m_effectiveGasPrice;

    runTxPipeline(ctx, bindings.precheckPolicy, bindings.errorPolicy);

    output.evmcResult = std::move(ctx.evmcResult);
    output.logs = std::move(ctx.kernelOutput.logs);

    co_await completeNormalTxAfterPipeline(ctx, feeCtx, input, feeParams, gasPool, output);

    if (isNormalPreExecutionReject(ctx.exitKind))
    {
        EVM_LOG(DEBUG) << LOG_DESC("opStackTxLifecycle entry reject abort")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code));
    }
    else
    {
        EVM_LOG(DEBUG) << LOG_DESC("opStackTxLifecycle done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                       << LOG_KV("gasUsed", output.gasUsed)
                       << LOG_KV("l1Fee", feeCtx.m_l1CostCharged);
    }

    co_return output;
}
}  // namespace bcos::evm
