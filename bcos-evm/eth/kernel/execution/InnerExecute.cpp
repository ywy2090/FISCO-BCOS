/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file InnerExecute.cpp
 *
 * geth: `evm.go` innerExecute — bridges `stateTransitionExecute` and `evm.Call` / CREATE.
 *
 * Top-level flow (message.depth == 0):
 *   1. clear_refund, clear transient storage (EIP-1153)
 *   2. prepareState — EIP-2929 tx-entry warm (sender, to, coinbase, precompiles, access list)
 *   3. build evmc_tx_context + EthHost (block hashes, hooks, ChainCallTargetPort)
 *   4. setupHostExecutionTarget — execution address + EIP-7702 authorization apply
 *   5. runCallFrame(TopLevel) — precompile fast path or evmone
 *   6. finalize — precompile-hit vs VM path; top-level sender nonce bump
 *
 * Nested flow (EthHost::call, depth > 0): steps 2 and 6 nonce policy differ; no prepareState.
 *
 * Journal ownership:
 *   - Precompile / empty-account hits: frame envelope already commit/revert; innerExecute builds
 * diff only
 *   - VM path success: innerExecute commit + finalize_self_destructs at top level
 */

#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/eip/Eip2929Gate.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/host/EthHost.h"
#include "bcos-evm/eth/kernel/execution/EvmCallFrame.h"
#include "bcos-evm/eth/kernel/execution/FrameScope.h"
#include "bcos-evm/eth/kernel/execution/PrepareState.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-utilities/BoostLog.h"
#include "eth/RevisionConfig.h"
#include "eth/state/BlockInfo.hpp"
#include "eth/state/StateDiff.hpp"
#include "eth/state/StateKeyHash.hpp"
#include "eth/state/Transaction.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace bcos::evm
{
namespace
{

/// Map evmc_message fields into `state::Transaction` for prepareState (CALL to / CREATE omit to).
state::Transaction toStateTransaction(const evmc_message& message)
{
    state::Transaction transaction;
    transaction.from = message.sender;
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
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

/// Block + tx header fields exposed to evmone host callbacks (BLOCKHASH, BASEFEE, …).
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

/// EIP-7702: apply set-code authorizations before first CALL frame (geth: applyAuthorizationList).
/// Bumps sender nonce here when auth list present; `bumpTopLevelSenderNonce` skips duplicate bump.
/// Warms delegation target under EIP-2929 when enabled.
void apply7702TxAuthorizationsIfNeeded(
    state::State& state, InnerExecuteInput const& input, evmc_address const& codeAddress)
{
    if (input.message.kind == EVMC_CREATE || input.message.kind == EVMC_CREATE2)
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
    if (execution::isEip2929Enabled(input.revisionConfig) && !state::isZeroAddress(codeAddress))
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

/// Tx-entry setup: EIP-1153 reset (depth 0) + prepareState warm set (geth state.Prepare).
void prepareTxEntry(state::State& state, InnerExecuteInput const& input)
{
    if (input.message.depth == 0)
    {
        state.clearAllTransientStorage();
    }
    auto const transaction = toStateTransaction(input.message);
    std::optional<evmc_address> createCodeAddress;
    if (input.message.kind == EVMC_CREATE || input.message.kind == EVMC_CREATE2)
    {
        createCodeAddress = input.message.code_address;
    }
    execution::prepareState(state, input.revisionConfig, input.callTargetPort, transaction,
        input.blockInfo, input.accessList, input.web3TypedTxKind, createCodeAddress);
}

/// Bind host execution address for CALL/STATICCALL/DELEGATECALL; CREATE skips (frame routing owns
/// it).
void setupHostExecutionTarget(
    state::EthHost& host, state::State& state, InnerExecuteInput const& input)
{
    if (input.message.kind == EVMC_CREATE || input.message.kind == EVMC_CREATE2)
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

/// Top-level sender nonce +1 after successful execution (geth outer nonce bump).
/// Skipped when EIP-7702 auth list already bumped, or orchestration sets
/// skipTopLevelSenderNonceBump.
void bumpTopLevelSenderNonce(state::State& state, InnerExecuteInput const& input)
{
    if (input.message.depth != 0 || state::isZeroAddress(input.message.sender))
    {
        return;
    }
    bool const authPrebumped = input.message.kind != EVMC_CREATE &&
                               input.message.kind != EVMC_CREATE2 && input.revisionConfig.eip7702 &&
                               input.authorizationListPresent && !input.authorizations.empty();
    if (authPrebumped || input.skipTopLevelSenderNonceBump)
    {
        return;
    }
    state.set_nonce(input.message.sender, state.get_nonce(input.message.sender) + 1);
}

/// Precompile / empty-account fast path: envelope already finalized journal inside runCallFrame.
InnerExecuteOutput finalizeEnvelopeFrame(state::State& state, InnerExecuteInput const& input,
    execution::FrameResult&& fr, state::EthHost& host)
{
    InnerExecuteOutput output;
    output.result = std::move(fr.result);
    output.logs = host.take_logs();
    EVM_LOG(TRACE) << LOG_DESC("innerExecute envelope")
                   << LOG_KV("status", trace::evmcStatus(output.result.status_code))
                   << LOG_KV("gasLeft", output.result.gas_left);
    output.gasRefund = fr.gasRefund;
    bumpTopLevelSenderNonce(state, input);
    output.stateDiff = state.build_diff();
    return output;
}

/// Normal VM / CREATE path: commit journal on success, always emit diff for outer pipeline.
InnerExecuteOutput finalizeAfterFrame(state::State& state, InnerExecuteInput const& input,
    execution::FrameResult&& fr, state::EthHost& host)
{
    InnerExecuteOutput output;
    output.result = std::move(fr.result);
    output.logs = host.take_logs();

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

InnerExecuteOutput innerExecute(InnerExecuteInput input)
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
        input.extension, input.callTargetPort);
    setupHostExecutionTarget(host, state, input);

    execution::CallFrameContext frameCtx{state, *input.vm, input.revisionConfig, input.extension,
        txContext.tx_origin, host.execution_address_ref(), input.callTargetPort};

    auto const scope =
        input.message.depth == 0 ? execution::FrameScope::TopLevel : execution::FrameScope::Nested;
    auto fr = execution::runCallFrame(frameCtx, input.message, scope, host);

    if (fr.envelopeComplete)
    {
        return finalizeEnvelopeFrame(state, input, std::move(fr), host);
    }
    return finalizeAfterFrame(state, input, std::move(fr), host);
}

}  // namespace bcos::evm
