#include "bcos-evm/opstack/OpStackTxLifecycle.h"

#include "bcos-evm/eth/pipeline/StateTransitionExecute.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-evm/opstack/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/OpStackNormalTxFeeCoordinator.h"
#include "bcos-evm/opstack/OpStackOrchestrationProfile.h"
#include "bcos-evm/opstack/OpStackPipelineInternals.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
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
}  // namespace

task::Task<OpStackExecutionResult> runOpStackTxLifecycle(OpStackExecutionRequest input)
{
    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("opstack", input.blockInfo.number, input.txHash));

    OpStackExecutionResult output;
    StateTransitionContext ctx{
        *input.stateView, input.message, input.revisionConfig, bcos::u256(0)};
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
    ctx.wireExecutionEnvironment(input.vm, nullptr, &chainAdapter);

    auto const feeParams = loadOpStackFeeParams(ctx.state);
    input.opTxExecutor.m_l1CostFunc = wireL1CostFuncWithState(input.forkSchedule, ctx.state);
    input.opTxExecutor.m_operatorCostFunc =
        wireOperatorCostFuncWithState(input.forkSchedule, ctx.state);

    OpStackFeeSidecar sidecar;
    sidecar.floorDataGas = input.floorDataGas;
    OpStackSettlementFacade view{ctx, input, sidecar};

    // chainAdapter + wireExecutionEnvironment inject vm/chainPort into ctx.
    // bindingsCtx is orchestration policy bind input only.
    OpStackOrchestrationProfile::BindingsContext bindingsCtx{input, view};
    auto bindings = OpStackOrchestrationProfile::bind(bindingsCtx);

    trace::logMessageContext(input.message);

    bindings.hooks.lifecycleCheckEntryRules(ctx);
    if (ctx.earlyExit)
    {
        output.evmcResult = std::move(ctx.evmcResult);
        co_return output;
    }

    auto gasPool = GasPoolHooks{
        .subGas = input.gasPoolSubGasHook,
        .returnGas = input.gasPoolReturnGasHook,
    };

    if (view.isDeposit())
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

        stateTransitionExecute(ctx, bindings.hooks, bindings.errorPolicy);

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

    OpStackNormalTxFeeCoordinator settlement{input.opTxExecutor};
    if (!co_await settlement.buyGas(view, gasPool, output))
    {
        co_return output;
    }

    ctx.gasPrice = sidecar.effectiveGasPrice;

    stateTransitionExecute(ctx, bindings.hooks, bindings.errorPolicy);

    output.evmcResult = std::move(ctx.evmcResult);
    output.logs = std::move(ctx.kernelOutput.logs);

    co_await settlement.completeAfterPipeline(view, feeParams, gasPool, output);

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
                       << LOG_KV("l1Fee", sidecar.l1CostCharged);
    }

    co_return output;
}
}  // namespace bcos::evm
