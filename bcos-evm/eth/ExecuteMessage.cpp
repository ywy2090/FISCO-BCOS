/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file ExecuteMessage.cpp
 */

#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/execution/warmTransactionEntry.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <optional>
#include <stdexcept>

namespace bcos::evm
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
    if (state::isCreateKind(input.message.kind))
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
    if (input.revisionConfig.warm_access && !state::isZeroAddress(codeAddress))
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
}  // namespace

ExecuteMessageOutput executeMessage(ExecuteMessageInput input)
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

    ExecuteMessageOutput output;
    std::optional<state::State> stateCopy;
    auto& state = resolveState(*input.stateView, stateCopy);
    state.clear_refund();

    auto const transaction = toStateTransaction(input.message);
    std::optional<evmc_address> createCodeAddress;
    if (isCreateKind(input.message.kind))
    {
        createCodeAddress = input.message.code_address;
    }

    if (input.message.depth == 0)
    {
        trace::logMessageContext(input.message);
    }
    else
    {
        EVM_LOG(TRACE) << LOG_DESC("executeMessage nested")
                       << LOG_KV("kind", trace::callKind(input.message.kind))
                       << LOG_KV("depth", input.message.depth) << LOG_KV("gas", input.message.gas)
                       << LOG_KV("code", trace::evmcAddress(input.message.code_address));
    }

    execution::warmTransactionEntry(state, input.revisionConfig, transaction, input.blockInfo,
        input.txProps, input.accessList, input.web3TypedTxKind, createCodeAddress);

    auto txContext = buildTxContext(input.blockInfo, input.message);
    txContext.tx_gas_price = state::toEvmC(input.gasPrice);
    state::EthHost host(state, txContext, input.revisionConfig, *input.vm, input.blockHashes,
        input.extension, input.fixStorageStatus);
    if (!isCreateKind(input.message.kind))
    {
        auto codeAddress = input.message.code_address;
        if (state::isZeroAddress(codeAddress))
        {
            codeAddress = input.message.recipient;
        }
        host.set_execution_address(codeAddress);
        apply7702TxAuthorizationsIfNeeded(state, input, codeAddress);
    }

    execution::FrameContext frameCtx{state, *input.vm, input.revisionConfig, input.extension,
        txContext.tx_origin, host.execution_address_ref(), input.fixNonceInit};

    auto const scope =
        input.message.depth == 0 ? execution::FrameScope::TopLevel : execution::FrameScope::Nested;
    auto fr = execution::runExecutionFrame(frameCtx, input.message, scope, host);

    output.result = std::move(fr.result);
    output.logs = host.take_logs();

    if (fr.precompileHit)
    {
        EVM_LOG(TRACE) << LOG_DESC("executeMessage precompile")
                       << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                       << LOG_KV("gasLeft", output.result.gas_left);
        output.gasRefund = fr.gasRefund;
        output.stateDiff = state.build_diff();
        return output;
    }

    if (output.result.status_code == EVMC_SUCCESS)
    {
        if (input.message.depth == 0 && !state::isZeroAddress(input.message.sender))
        {
            bool const authPrebumped =
                !isCreateKind(input.message.kind) && input.revisionConfig.eip7702 &&
                input.authorizationListPresent && !input.authorizations.empty();
            if (!authPrebumped)
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

}  // namespace bcos::evm
