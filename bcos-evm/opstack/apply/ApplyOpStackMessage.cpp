/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file ApplyOpStackMessage.cpp
 */

#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"

#include "bcos-evm/eth/kernel/state-transition/StateTransitionExecute.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-evm/opstack/adapter/OpStackChainCallTargetAdapter.h"
#include "bcos-evm/opstack/apply/OpStackEvmResult.h"
#include "bcos-evm/opstack/apply/OpStackStateTransitionBindings.h"
#include "bcos-evm/opstack/fee/OpStackFeeParams.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/settlement/OpStackNormalTxFeeCoordinator.h"
#include "bcos-evm/opstack/settlement/OpStackSettlementProjection.h"
#include "bcos-evm/opstack/settlement/OpStackTxFinalize.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "eth/RevisionConfig.h"
#include "eth/eip/Eip7702.h"
#include "eth/gas/GasSettlementTypes.h"
#include "eth/kernel/EVMCResult.h"
#include "eth/kernel/state-transition/StateTransitionContext.h"
#include "eth/state/BlockInfo.hpp"
#include "eth/state/State.hpp"
#include "eth/state/StateDiff.hpp"
#include "eth/state/Transaction.hpp"
#include "opstack/apply/OpStackStateTransitionErrorPolicy.h"
#include "opstack/apply/OpStackStateTransitionHooks.h"
#include "opstack/policy/OpStackForkSchedule.h"
#include "opstack/settlement/OpStackFeeSettlement.h"
#include "opstack/settlement/OpStackFeeSidecar.h"
#include "opstack/types/OpStackDepositTx.h"
#include "opstack/types/OpStackReceiptMeta.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace bcos::evm
{
namespace
{
/// Reserve gas from block pool; no-op when hook unset (tests / unconstrained callers).
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

task::Task<OpStackMessageResult> applyOpStackMessage(OpStackMessageRequest input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("applyOpStackMessage requires stateView/vm/hashImpl");
    }

    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("opstack", input.blockInfo.number, input.txHash));

    OpStackMessageResult output;
    StateTransitionContext ctx{
        *input.stateView, input.message, input.revisionConfig, bcos::u256(0)};
    ctx.inputs.vm = input.vm;
    ctx.inputs.hashImpl = input.hashImpl;
    ctx.inputs.blockInfo = input.blockInfo;
    ctx.inputs.blockHashes = input.blockHashes;
    ctx.inputs.accessList = input.accessList;
    ctx.inputs.authorizationListPresent = input.authorizationListPresent;
    ctx.inputs.authorizations = input.authorizations;
    ctx.inputs.web3TypedTxKind = input.web3TypedTxKind;
    ctx.inputs.blobVersionedHashes = input.blobVersionedHashes;

    // OpStack predeploy classification + dispatch (L1Block, GasPriceOracle, …).
    OpStackChainCallTargetAdapter chainAdapter(
        &ctx.state, input.blockInfo.baseFee, input.forkSchedule, input.blockInfo.timestamp);
    ctx.wireExecutionEnvironment(input.vm, nullptr, &chainAdapter);

    auto const feeParams = loadOpStackFeeParams(ctx.state);
    input.opTxExecutor.m_l1CostFunc = wireL1CostFuncWithState(input.forkSchedule, ctx.state);
    input.opTxExecutor.m_operatorCostFunc =
        wireOperatorCostFuncWithState(input.forkSchedule, ctx.state);

    OpStackFeeSidecar sidecar;
    sidecar.floorDataGas = input.floorDataGas;
    OpStackSettlementProjection view{ctx, input, sidecar};

    // chainAdapter + wireExecutionEnvironment inject vm/callTargetPort into ctx.
    // bindingsCtx is orchestration policy bind input only.
    OpStackStateTransitionBindings::Context bindingsCtx{input, view};
    auto bindings = OpStackStateTransitionBindings::bind(bindingsCtx);

    trace::logMessageContext(input.message);

    // OpStack entry rules (nonce, fee caps, deposit-specific checks) may set ctx.earlyExit.
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

    // ── Deposit path: L1-originated system tx; no buyGas / L1 fee settlement ──
    if (view.isDeposit())
    {
        if (!acquireGasPool(gasPool, ctx.originalGasLimit))
        {
            output.evmcResult = makeOutOfGasLimitResult();
            co_return output;
        }

        output.receiptMeta.depositNonce = ctx.state.get_nonce(input.message.sender);
        output.receiptMeta.depositReceiptVersion = OP_CANYON_DEPOSIT_RECEIPT_VERSION;
        // Mint ETH credited by L1 deposit before execution (op-geth: mint on deposit).
        if (input.depositTx.has_value() && input.depositTx->mint.has_value() &&
            *input.depositTx->mint > 0)
        {
            ctx.state.set_balance(input.message.sender,
                ctx.state.get_balance(input.message.sender) + *input.depositTx->mint);
        }

        ctx.state.checkpoint();

        stateTransitionExecute(ctx, bindings.hooks, bindings.errorPolicy);

        output.evmcResult = std::move(ctx.evmcResult);
        output.logs = std::move(ctx.logs);
        output.gasAccounting = ctx.gasAccounting;

        auto settled =
            co_await settleDeposit(ctx, ctx.exitKind, output.evmcResult.status_code, gasPool);

        EVM_LOG(DEBUG) << LOG_DESC("applyOpStackMessage deposit done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                       << LOG_KV("gasUsed", settled.gasUsed);

        output.gasUsed = settled.gasUsed;
        output.stateDiff = ctx.state.build_diff();
        co_return output;
    }

    // ── Normal L2 user tx: buy gas, execute, refund + L1/operator fee finalize ──
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
    output.logs = std::move(ctx.logs);
    output.gasAccounting = ctx.gasAccounting;

    co_await settlement.completeAfterPipeline(view, feeParams, gasPool, output);

    if (isNormalPreExecutionReject(ctx.exitKind))
    {
        EVM_LOG(DEBUG) << LOG_DESC("applyOpStackMessage entry reject abort")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code));
    }
    else
    {
        EVM_LOG(DEBUG) << LOG_DESC("applyOpStackMessage done")
                       << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                       << LOG_KV("status", trace::evmcStatus(output.evmcResult.status_code))
                       << LOG_KV("gasUsed", output.gasUsed)
                       << LOG_KV("l1Fee", sidecar.l1CostCharged);
    }

    co_return output;
}

}  // namespace bcos::evm
