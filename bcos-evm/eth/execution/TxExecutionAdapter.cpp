/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file TxExecutionAdapter.cpp
 */

#include "bcos-evm/eth/execution/TxExecutionAdapter.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/execution/Eip2929Access.h"
#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/execution/WarmTransactionEntry.h"
#include "bcos-evm/eth/state/EthHost.hpp"
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
    state::State& state, ExecuteMessageInput const& input, evmc_address const& codeAddress)
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

state::State& resolveState(
    state::EvmStateReader const& stateView, std::optional<state::State>& stateCopy)
{
    if (auto* statePtr = dynamic_cast<state::State const*>(&stateView); statePtr != nullptr)
    {
        return *const_cast<state::State*>(statePtr);
    }

    stateCopy.emplace(stateView);
    return *stateCopy;
}

void logEntry(ExecuteMessageInput const& input)
{
    if (input.message.depth == 0)
    {
        trace::logMessageContext(input.message);
        return;
    }
    EVM_LOG(TRACE) << LOG_DESC("executeMessage nested")
                   << LOG_KV("kind", trace::callKind(input.message.kind))
                   << LOG_KV("depth", input.message.depth) << LOG_KV("gas", input.message.gas)
                   << LOG_KV("code", trace::evmcAddress(input.message.code_address));
}

void prepareTxEntry(state::State& state, ExecuteMessageInput const& input)
{
    auto const transaction = toStateTransaction(input.message);
    std::optional<evmc_address> createCodeAddress;
    if (isCreateKind(input.message.kind))
    {
        createCodeAddress = input.message.code_address;
    }
    execution::warmTransactionEntry(state, input.revisionConfig, transaction, input.blockInfo,
        input.txProps, input.accessList, input.web3TypedTxKind, createCodeAddress);
}

void setupHostExecutionTarget(
    state::EthHost& host, state::State& state, ExecuteMessageInput const& input)
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

ExecuteMessageOutput finalizePrecompileHit(
    state::State& state, FrameResult&& fr, state::EthHost& host)
{
    ExecuteMessageOutput output;
    output.result = std::move(fr.result);
    output.logs = host.take_logs();
    EVM_LOG(TRACE) << LOG_DESC("executeMessage precompile")
                   << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                   << LOG_KV("gasLeft", output.result.gas_left);
    output.gasRefund = fr.gasRefund;
    output.stateDiff = state.build_diff();
    return output;
}

ExecuteMessageOutput finalizeAfterFrame(
    state::State& state, ExecuteMessageInput const& input, FrameResult&& fr, state::EthHost& host)
{
    ExecuteMessageOutput output;
    output.result = std::move(fr.result);
    output.logs = host.take_logs();

    if (output.result.status_code == EVMC_SUCCESS)
    {
        if (input.message.depth == 0 && !state::isZeroAddress(input.message.sender))
        {
            bool const authPrebumped =
                !isCreateKind(input.message.kind) && input.revisionConfig.eip7702 &&
                input.authorizationListPresent && !input.authorizations.empty();
            if (!authPrebumped && !input.skipTopLevelSenderNonceBump)
            {
                state.set_nonce(input.message.sender, state.get_nonce(input.message.sender) + 1);
            }
        }
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
        EVM_LOG(DEBUG) << LOG_DESC("executeMessage done")
                       << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                       << LOG_KV("gasLeft", output.result.gas_left)
                       << LOG_KV("gasRefund", output.gasRefund)
                       << LOG_KV("logCount", output.logs.size());
    }
    else
    {
        EVM_LOG(TRACE) << LOG_DESC("executeMessage nested done")
                       << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                       << LOG_KV("gasLeft", output.result.gas_left);
    }
    return output;
}
}  // namespace

ExecuteMessageOutput TxExecutionAdapter::run(ExecuteMessageInput input)
{
    if (input.stateView == nullptr || input.vm == nullptr)
    {
        throw std::invalid_argument("executeMessage requires stateView/vm");
    }

    std::optional<trace::EvmTraceScope> traceScope;
    if (input.message.depth == 0 && !trace::currentTraceId().has_value())
    {
        traceScope.emplace(trace::makeTraceContext("kernel", input.blockInfo.number, input.txHash));
    }

    std::optional<state::State> stateCopy;
    auto& state = resolveState(*input.stateView, stateCopy);
    state.clear_refund();

    logEntry(input);
    prepareTxEntry(state, input);

    auto txContext = buildTxContext(input.blockInfo, input.message);
    txContext.tx_gas_price = state::toEvmC(input.gasPrice);
    state::EthHost host(state, txContext, input.revisionConfig, *input.vm, input.blockHashes,
        input.extension, input.fixStorageStatus);
    setupHostExecutionTarget(host, state, input);

    execution::FrameContext frameCtx{state, *input.vm, input.revisionConfig, input.extension,
        txContext.tx_origin, host.execution_address_ref(), input.fixNonceInit};

    auto const scope = input.message.depth == 0 ? FrameScope::TopLevel : FrameScope::Nested;
    auto fr = runExecutionFrame(frameCtx, input.message, scope, host);

    if (fr.precompileHit)
    {
        return finalizePrecompileHit(state, std::move(fr), host);
    }
    return finalizeAfterFrame(state, input, std::move(fr), host);
}

}  // namespace bcos::evm::execution
