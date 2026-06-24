#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/eth/orchestration/TxPipeline.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-evm/opstack/OpStackPipelineHookBinder.h"
#include "bcos-evm/opstack/OpStackPipelineInternals.h"
#include "bcos-evm/opstack/OpStackTxPrecheck.h"
#include "bcos-evm/opstack/OpStackVmHostPolicy.h"
#include "bcos-evm/opstack/fee/OpStackFee.h"
#include <algorithm>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
struct GasPoolReturnGuard
{
    std::function<void(uint64_t, uint64_t)>* hook{};
    uint64_t gasRemaining{0};
    uint64_t gasUsed{0};
    bool armed{false};

    ~GasPoolReturnGuard()
    {
        if (armed && hook && *hook)
        {
            (*hook)(gasRemaining, gasUsed);
        }
    }
};

void returnDepositPoolGas(
    OpStackExecutionRequest const& input, OpStackTxFeeLedger::OpStackTxExecutionData const& txData)
{
    if (!input.gasPoolReturnGasHook)
    {
        return;
    }
    auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit));
    auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasUsed));
    auto const gasRemaining = gasLimit > gasUsed ? gasLimit - gasUsed : 0;
    input.gasPoolReturnGasHook(gasRemaining, gasUsed);
}
}  // namespace

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

    OpStackTxFeeLedger::OpStackTxExecutionData txData;
    txData.m_call = input.call;
    txData.m_isDepositTx = isDepositTx(input);
    txData.m_state = &ctx.state;
    txData.m_message = input.message;
    txData.m_gasTipCap = input.gasTipCap;
    txData.m_gasFeeCap = input.gasFeeCap;
    txData.m_hasGasFeeCap = true;
    txData.m_effectiveGasPrice =
        resolveEffectiveGasPrice(input.gasTipCap, input.gasFeeCap, input.blockInfo.baseFee);
    txData.m_gasLimit = input.message.gas;
    txData.m_blockInfo = input.blockInfo;
    txData.m_skipNonceChecks = input.skipNonceChecks;
    txData.m_skipTransactionChecks = input.skipTransactionChecks;
    txData.m_noBaseFee = input.noBaseFee;
    txData.m_floorDataGas = input.floorDataGas;
    txData.m_accessList = input.accessList;
    txData.m_web3TypedTxKind = input.web3TypedTxKind;
    txData.m_authTupleCount = static_cast<uint64_t>(input.authorizations.size());
    txData.m_blobGasFeeCap = input.blobGasFeeCap;
    txData.m_blobVersionedHashes = input.blobVersionedHashes;
    txData.m_rollupCostData = input.rollupCostData;

    trace::logMessageContext(input.message);

    if (auto preCheckError = opStackTxPrecheck(input, ctx.state); preCheckError.has_value())
    {
        output.evmcResult = std::move(*preCheckError);
        co_return output;
    }

    if (txData.m_isDepositTx)
    {
        output.receiptMeta.depositNonce = ctx.state.get_nonce(input.message.sender);
        if (input.depositTx.has_value() && input.depositTx->mint.has_value() &&
            *input.depositTx->mint > 0)
        {
            ctx.state.set_balance(input.message.sender,
                ctx.state.get_balance(input.message.sender) + *input.depositTx->mint);
        }

        ctx.state.checkpoint();

        OpStackPipelineHookBinder::HookBindingContext session{input, txData};
        auto hooks = OpStackPipelineHookBinder::buildHooks(session);
        // TODO: OrchestrationErrorPolicy (candidate 4)
        hooks.txHandleIntrinsicGasFailure = [](TxPipelineContext& c, IntrinsicDebitFailure) {
            c.evmcResult = makeOutOfGasLimitResult();
        };
        hooks.txHandlePipelineException = [](TxPipelineContext& c, std::exception_ptr) {
            c.evmcResult = makeInternalErrorResult();
        };
        runTxPipeline(ctx, hooks);

        output.evmcResult = std::move(ctx.evmcResult);
        EVM_LOG(DEBUG) << LOG_DESC("opStackExecute deposit done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                       << LOG_KV("gasUsed", txData.m_gasUsed);

        output.logs = std::move(ctx.kernelOutput.logs);

        if (ctx.exitKind == TxPipelineExitKind::Completed &&
            output.evmcResult.status_code == EVMC_SUCCESS)
        {
            auto const nonce = ctx.state.get_nonce(input.message.sender);
            ctx.state.set_nonce(input.message.sender, nonce + 1);
            ctx.state.commit();
        }
        else
        {
            if (ctx.state.has_checkpoint())
            {
                ctx.state.revert();
            }
            auto const nonce = ctx.state.get_nonce(input.message.sender);
            ctx.state.set_nonce(input.message.sender, nonce + 1);
            if (ctx.exitKind != TxPipelineExitKind::Completed)
            {
                txData.m_gasUsed = std::max<int64_t>(0, txData.m_gasLimit);
            }
        }

        output.gasUsed = txData.m_gasUsed;
        output.stateDiff = ctx.state.build_diff();
        returnDepositPoolGas(input, txData);
        co_return output;
    }

    if (input.gasPoolSubGasHook)
    {
        auto const gasLimitForPool = static_cast<uint64_t>(std::max<int64_t>(0, input.message.gas));
        if (!input.gasPoolSubGasHook(gasLimitForPool))
        {
            output.evmcResult = makeOutOfGasLimitResult();
            co_return output;
        }
    }

    GasPoolReturnGuard guard{&input.gasPoolReturnGasHook};

    auto buyGasOk = co_await input.opTxExecutor.buyGas(txData);
    if (!buyGasOk)
    {
        output.evmcResult = std::move(*txData.m_evmcResult);
        co_return output;
    }

    guard.armed = true;
    ctx.gasPrice = txData.m_effectiveGasPrice;

    OpStackPipelineHookBinder::HookBindingContext session{input, txData};
    auto hooks = OpStackPipelineHookBinder::buildHooks(session);
    // TODO: OrchestrationErrorPolicy (candidate 4)
    hooks.txHandleIntrinsicGasFailure = [](TxPipelineContext& c, IntrinsicDebitFailure) {
        c.evmcResult = makeOutOfGasLimitResult();
    };
    hooks.txHandlePipelineException = [](TxPipelineContext& c, std::exception_ptr) {
        c.evmcResult = makeInternalErrorResult();
    };
    runTxPipeline(ctx, hooks);

    output.evmcResult = std::move(ctx.evmcResult);
    output.logs = std::move(ctx.kernelOutput.logs);
    EVM_LOG(DEBUG) << LOG_DESC("opStackExecute done")
                   << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                   << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                   << LOG_KV("gasUsed", txData.m_gasUsed)
                   << LOG_KV("l1Fee", txData.m_l1CostCharged);
    if (ctx.exitKind != TxPipelineExitKind::Completed)
    {
        if (ctx.exitKind == TxPipelineExitKind::IntrinsicRejected ||
            ctx.exitKind == TxPipelineExitKind::GasAffordRejected)
        {
            // EVM never ran (intrinsic or floor-gas rejection): refund the full buyGas
            // pre-deduction so the sender is not charged for a tx that should be rejected
            // with only a nonce bump (no balance change).
            txData.m_gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit));
            txData.m_gasUsed = 0;
        }
        else
        {
            OpStackPipelineHookBinder::applySettlement(session, output.evmcResult);
        }
    }

    co_await input.opTxExecutor.refundGas(txData);

    output.gasUsed = txData.m_gasUsed;
    output.receiptMeta.l1Fee = txData.m_l1CostCharged;
    if (isOpStackIsthmus(input.forkSchedule, txData.m_blockInfo.timestamp) &&
        input.opTxExecutor.m_operatorCostFunc)
    {
        auto const gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasUsed));
        output.receiptMeta.operatorFee =
            input.opTxExecutor.m_operatorCostFunc(gasUsed, txData.m_blockInfo.timestamp);
        if (feeParams.operatorFeeScalar != 0 || feeParams.operatorFeeConstant != 0)
        {
            output.receiptMeta.operatorFeeScalar = feeParams.operatorFeeScalar;
            output.receiptMeta.operatorFeeConstant = feeParams.operatorFeeConstant;
        }
    }
    output.stateDiff = ctx.state.build_diff();
    guard.gasRemaining = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasRemaining));
    guard.gasUsed = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasUsed));
    co_return output;
}
}  // namespace bcos::evm
