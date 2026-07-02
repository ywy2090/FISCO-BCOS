/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file TxExecutionRunner.cpp
 */

#include "bcos-evm/eth/kernel/execution/TxExecutionRunner.h"
#include "bcos-evm/eth/eip/Eip2929Gate.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/kernel/execution/EvmCallFrame.h"
#include "bcos-evm/eth/kernel/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <optional>
#include <stdexcept>

namespace bcos::evm::execution
{
namespace
{
bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

state::Transaction toStateTransaction(const evmc_message& message)
{
    state::Transaction transaction;
    transaction.from = message.sender;
    if (!isCreateKind(message.kind))
    {
        auto to = message.code_address;
        if (state::isZeroAddress(to))
        {
            to = message.recipient;
        }
        transaction.to = to;
    }
    transaction.data.assign(message.input_data, message.input_data + message.input_size);
    transaction.value = state::fromEvmC(message.value);
    transaction.gasLimit = message.gas;
    return transaction;
}

evmc_tx_context buildTxContext(const state::BlockInfo& block, const evmc_message& message)
{
    evmc_tx_context context{};
    context.tx_origin = message.sender;
    context.block_coinbase = block.coinbase;
    context.block_number = block.number;
    context.block_timestamp = block.timestamp;
    context.block_gas_limit = block.gasLimit;
    context.block_prev_randao = block.prevRandao;
    context.chain_id = state::toEvmC(block.chainId);
    context.block_base_fee = state::toEvmC(block.baseFee);
    context.blob_base_fee = state::toEvmC(block.blobBaseFee);
    return context;
}

void apply7702TxAuthorizationsIfNeeded(
    state::State& state, InnerExecuteInput const& input, evmc_address const& codeAddress)
{
    if (isCreateKind(input.message.kind))
    {
        return;
    }
    if (!input.revisionConfig.eip7702 || !input.authorizationListPresent ||
        input.authorizations.empty())
    {
        return;
    }
    state.checkpoint();
    if (!state::isZeroAddress(input.message.sender))
    {
        auto const senderNonce = state.get_nonce(input.message.sender);
        state.set_nonce(input.message.sender, senderNonce + 1);
    }
    applyAuthorizations(state, input.authorizations, input.blockInfo.chainId);
    if (isEip2929Enabled(input.revisionConfig) && !state::isZeroAddress(codeAddress))
    {
        warmDelegationTarget(state, codeAddress);
    }
    state.commit();
}

void logEntry(InnerExecuteInput const& input)
{
    if (input.message.depth == 0)
    {
        trace::logMessageContext(input.message);
        return;
    }
    EVM_LOG(TRACE) << LOG_DESC("innerExecute nested")
                   << LOG_KV("kind", trace::callKind(input.message.kind))
                   << LOG_KV("depth", input.message.depth) << LOG_KV("gas", input.message.gas)
                   << LOG_KV("code", trace::evmcAddress(input.message.code_address));
}

void prepareTxEntry(state::State& state, InnerExecuteInput const& input)
{
    if (input.message.depth == 0)
    {
        state.clearAllTransientStorage();
    }
    auto const transaction = toStateTransaction(input.message);
    std::optional<evmc_address> createCodeAddress;
    if (isCreateKind(input.message.kind))
    {
        createCodeAddress = input.message.code_address;
    }
    execution::warmTransactionEntry(state, input.revisionConfig, input.chainPort, transaction,
        input.blockInfo, input.txProps, input.accessList, input.web3TypedTxKind, createCodeAddress);
}

void setupHostExecutionTarget(
    state::EthHost& host, state::State& state, InnerExecuteInput const& input)
{
    if (isCreateKind(input.message.kind))
    {
        return;
    }
    auto codeAddress = input.message.code_address;
    if (state::isZeroAddress(codeAddress))
    {
        codeAddress = input.message.recipient;
    }
    host.set_execution_address(codeAddress);
    apply7702TxAuthorizationsIfNeeded(state, input, codeAddress);
}

/// Bump the top-level sender nonce for an INCLUDED transaction that executed the EVM.
///
/// geth increments the sender nonce before execution for every included tx (CALL at
/// state_transition.go:620; CREATE inside evm.create at evm.go:499, before the revert
/// snapshot), so the bump survives EVM failure (revert/OOG). Reject-class txs never
/// reach the kernel (they early-exit in stateTransitionExecute before onInvokeInnerExecute),
/// so calling this on both success and failure of the executed frame matches geth exactly.
///
/// EIP-7702 txs already bumped the sender nonce during authorization application
/// (apply7702TxAuthorizationsIfNeeded), so they are skipped here to avoid a double bump.
void bumpTopLevelSenderNonce(state::State& state, InnerExecuteInput const& input)
{
    if (input.message.depth != 0 || state::isZeroAddress(input.message.sender))
    {
        return;
    }
    bool const authPrebumped = !isCreateKind(input.message.kind) && input.revisionConfig.eip7702 &&
                               input.authorizationListPresent && !input.authorizations.empty();
    if (authPrebumped || input.skipTopLevelSenderNonceBump)
    {
        return;
    }
    state.set_nonce(input.message.sender, state.get_nonce(input.message.sender) + 1);
}

InnerExecuteOutput finalizePrecompileHit(
    state::State& state, InnerExecuteInput const& input, FrameResult&& fr, state::EthHost& host)
{
    InnerExecuteOutput output;
    output.result = std::move(fr.result);
    output.logs = host.take_logs();
    EVM_LOG(TRACE) << LOG_DESC("innerExecute precompile")
                   << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                   << LOG_KV("gasLeft", output.result.gas_left);
    output.gasRefund = fr.gasRefund;
    // A top-level tx targeting a precompile is still an included CALL: bump the sender
    // nonce like geth. The envelope already committed/reverted its own checkpoint.
    bumpTopLevelSenderNonce(state, input);
    output.stateDiff = state.build_diff();
    return output;
}

InnerExecuteOutput finalizeAfterFrame(
    state::State& state, InnerExecuteInput const& input, FrameResult&& fr, state::EthHost& host)
{
    InnerExecuteOutput output;
    output.result = std::move(fr.result);
    output.logs = host.take_logs();

    // geth bumps the sender nonce for every included tx regardless of EVM outcome.
    bumpTopLevelSenderNonce(state, input);

    if (output.result.status_code == EVMC_SUCCESS)
    {
        output.gasRefund = static_cast<int64_t>(state.get_refund());
        state.commit();
        state.finalize_self_destructs();
        output.stateDiff = state.build_diff();
    }
    else
    {
        output.gasRefund = static_cast<int64_t>(state.get_refund());
        output.stateDiff = state.build_diff();
    }

    if (input.message.depth == 0)
    {
        EVM_LOG(DEBUG) << LOG_DESC("innerExecute done")
                       << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                       << LOG_KV("gasLeft", output.result.gas_left)
                       << LOG_KV("gasRefund", output.gasRefund)
                       << LOG_KV("logCount", output.logs.size());
    }
    else
    {
        EVM_LOG(TRACE) << LOG_DESC("innerExecute nested done")
                       << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                       << LOG_KV("gasLeft", output.result.gas_left);
    }
    return output;
}
}  // namespace

InnerExecuteOutput TxExecutionRunner::runEvmKernelTopLevel(InnerExecuteInput input)
{
    if (input.state == nullptr || input.vm == nullptr)
    {
        throw std::invalid_argument("innerExecute requires State owner and vm");
    }

    std::optional<trace::EvmTraceScope> traceScope;
    if (input.message.depth == 0 && !trace::currentTraceId().has_value())
    {
        traceScope.emplace(trace::makeTraceContext("kernel", input.blockInfo.number, input.txHash));
    }

    auto& state = *input.state;
    state.clear_refund();

    logEntry(input);
    prepareTxEntry(state, input);

    auto txContext = buildTxContext(input.blockInfo, input.message);
    txContext.tx_gas_price = state::toEvmC(input.gasPrice);
    state::EthHost host(state, txContext, input.revisionConfig, *input.vm, input.blockHashes,
        input.extension, input.chainPort);
    setupHostExecutionTarget(host, state, input);

    execution::FrameExecutionEnv frameCtx{state, *input.vm, input.revisionConfig, input.extension,
        txContext.tx_origin, host.execution_address_ref(), input.chainPort};

    auto const scope = input.message.depth == 0 ? FrameScope::TopLevel : FrameScope::Nested;
    auto fr = runCallFrame(frameCtx, input.message, scope, host);

    if (fr.precompileHit)
    {
        return finalizePrecompileHit(state, input, std::move(fr), host);
    }
    return finalizeAfterFrame(state, input, std::move(fr), host);
}

}  // namespace bcos::evm::execution
