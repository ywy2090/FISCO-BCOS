#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "bcos-evm/eth/orchestration/OrchestrationPipeline.h"
#include "bcos-evm/opstack/OpHostExtension.h"
#include "bcos-evm/opstack/OpStackFee.h"
#include "bcos-evm/opstack/OpStackGasSettlement.h"
#include "bcos-evm/opstack/OpStackPreCheck.h"
#include "bcos-evm/opstack/OpStackPreDebitEntry.h"
#ifdef BCOS_EVM_TESTING
#include "bcos-evm/opstack/OpStackExecuteMessageTestHook.h"
#endif
#include <algorithm>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
EVMCResult makeOutOfGasLimitResult()
{
    evmc_result failResult{};
    failResult.status_code = EVMC_OUT_OF_GAS;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::OutOfGasLimit);
}

EVMCResult makeInternalErrorResult()
{
    evmc_result failResult{};
    failResult.status_code = EVMC_INTERNAL_ERROR;
    failResult.gas_left = 0;
    return EVMCResult(failResult, protocol::TransactionStatus::Unknown);
}

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

void returnDepositPoolGas(OpStackExecuteViaHostInput const& input,
    OpStackTxExecutor::OpStackTxExecutionData const& txData)
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

task::Task<OpStackExecuteViaHostOutput> opStackExecuteViaHost(OpStackExecuteViaHostInput input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("opStackExecuteViaHost requires stateView/vm/hashImpl");
    }

    OpStackExecuteViaHostOutput output;
    OrchestrationContext ctx{*input.stateView, input.message, input.revisionConfig, bcos::u256(0)};
    ctx.txProps = input.txProps;
    ctx.inputs.vm = input.vm;
    ctx.inputs.hashImpl = input.hashImpl;
    ctx.inputs.blockInfo = input.blockInfo;
    ctx.inputs.blockHashes = input.blockHashes;
    ctx.inputs.accessList = input.accessList;
    ctx.inputs.authorizationListPresent = input.authorizationListPresent;
    ctx.inputs.authorizations = input.authorizations;
    ctx.inputs.web3TypedTxKind = input.web3TypedTxKind;

    OpHostExtension opHostExtension(&ctx.state);
    ctx.extension = &opHostExtension;

    auto const feeParams = loadOpStackFeeParams(ctx.state);
    input.opTxExecutor.m_l1CostFunc = wireL1CostFuncWithState(input.forkSchedule, ctx.state);
    input.opTxExecutor.m_operatorCostFunc =
        wireOperatorCostFuncWithState(input.forkSchedule, ctx.state);

    OpStackTxExecutor::OpStackTxExecutionData txData;
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

    auto applySettlement = [&](EVMCResult const& result) {
        auto const settlement =
            postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)),
                static_cast<uint64_t>(std::max<int64_t>(0, result.gas_left)),
                ctx.state.get_refund(), txData.m_floorDataGas);
        txData.m_gasRemaining = settlement.gasRemaining;
        txData.m_maxUsedGas = settlement.maxUsedGas;
        txData.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);
    };

    auto buildHooks = [&]() {
        OrchestrationHooks hooks;
        hooks.preDebitEntry = [&](OrchestrationContext& orchestrationCtx) {
            auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit));
            bcos::bytesConstRef inputData{
                orchestrationCtx.message.input_data, orchestrationCtx.message.input_size};
            if (auto preDebitError = opStackPreDebitEntry({.message = orchestrationCtx.message,
                    .state = orchestrationCtx.state,
                    .gasLimit = gasLimit,
                    .skipTransactionChecks = txData.m_skipTransactionChecks,
                    .inputData = inputData,
                    .floorDataGasOut = txData.m_floorDataGas});
                preDebitError.has_value())
            {
                orchestrationCtx.evmcResult = std::move(*preDebitError);
                orchestrationCtx.earlyExit = true;
            }
        };
        hooks.intrinsicPolicy.mode = IntrinsicDebitMode::OpStackEntry;
        hooks.intrinsicPolicy.authTupleCount = txData.m_authTupleCount;
        hooks.intrinsicPolicy.accessList = txData.m_accessList;
        hooks.intrinsicPolicy.web3TypedTxKind = txData.m_web3TypedTxKind;
        hooks.mapIntrinsicFailure = [](OrchestrationContext& orchestrationCtx,
                                        IntrinsicDebitFailure) {
            orchestrationCtx.evmcResult = makeOutOfGasLimitResult();
        };
        hooks.postSettle = [&](OrchestrationContext& orchestrationCtx) {
            applySettlement(orchestrationCtx.evmcResult);
        };
        hooks.mapException = [](OrchestrationContext& orchestrationCtx, std::exception_ptr) {
            orchestrationCtx.evmcResult = makeInternalErrorResult();
        };
#ifdef BCOS_EVM_TESTING
        hooks.executeMessageOverride = [](ExecuteMessageInput&& execInput) -> ExecuteMessageOutput {
            if (auto spyOutput = opstack::test::maybeCallExecuteMessageSpy(execInput);
                spyOutput.has_value())
            {
                return std::move(*spyOutput);
            }
            return executeMessage(std::move(execInput));
        };
#endif
        return hooks;
    };

    if (auto preCheckError = opStackPreCheck(input, ctx.state); preCheckError.has_value())
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

        auto hooks = buildHooks();
        runOrchestration(ctx, hooks);

        output.evmcResult = std::move(ctx.evmcResult);
        output.logs = std::move(ctx.kernelOutput.logs);

        if (ctx.exitKind == OrchestrationExitKind::KernelCompleted &&
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
            if (ctx.exitKind != OrchestrationExitKind::KernelCompleted)
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

    auto hooks = buildHooks();
    runOrchestration(ctx, hooks);

    output.evmcResult = std::move(ctx.evmcResult);
    output.logs = std::move(ctx.kernelOutput.logs);
    if (ctx.exitKind != OrchestrationExitKind::KernelCompleted)
    {
        applySettlement(output.evmcResult);
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
