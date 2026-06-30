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
 * @file ApplyFiscoMessage.cpp
 */

#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "bcos-evm/bcos/FiscoAddressDerivation.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoExecutionBundle.h"
#include "bcos-evm/bcos/FiscoPipelineInternals.h"
#include "bcos-evm/bcos/FiscoStateTransitionBindings.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/eth/execution/TxFeaturePrepare.h"
#include "bcos-evm/eth/state-transition/StateTransitionExecute.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
std::vector<protocol::LogEntry> convertLogs(const std::vector<state::LogEntry>& logs)
{
    std::vector<protocol::LogEntry> out;
    out.reserve(logs.size());
    for (auto const& log : logs)
    {
        std::span addressView(log.address.bytes, sizeof(log.address.bytes));
        h256s topics;
        topics.reserve(log.topics.size());
        for (auto const& topic : log.topics)
        {
            topics.emplace_back(state::fromEvmC(topic));
        }
        out.emplace_back(
            toHex<decltype(addressView), bcos::bytes>(addressView), std::move(topics), log.data);
    }
    return out;
}
}  // namespace

evmc_message deriveMessage(const FiscoTxAdapterInput& input)
{
    auto message = input.message;
    applyTopLevelCreateDerivation(message, FiscoTopLevelCreateParams{.web3Tx = input.web3Tx,
                                               .featureEvmAddress = input.featureEvmAddress,
                                               .blockNumber = input.blockNumber,
                                               .contextID = input.contextID,
                                               .seq = input.seq,
                                               .txNonce = input.nonce,
                                               .hashImpl = input.hashImpl});
    return message;
}

task::Task<FiscoExecutionResult> applyFiscoMessage(FiscoExecutionRequest input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("applyFiscoMessage requires stateView/vm/hashImpl");
    }

    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("fisco", input.blockInfo.number, input.txHash));

    FiscoExecutionResult output;
    output.executionContext.message = input.message;
    output.executionContext.revisionConfig = input.revisionConfig;

    auto const fixErrorHandling = input.revisionConfig.fix_error_handling;
    auto const eip7623Enabled = input.web3Tx && input.revisionConfig.eth().eip7623;

    StateTransitionContext ctx{
        *input.stateView, input.message, input.revisionConfig.eth(), input.gasPrice};
    ctx.inputs.vm = input.vm;
    ctx.inputs.hashImpl = input.hashImpl;
    ctx.inputs.blockInfo = input.blockInfo;
    ctx.inputs.blockHashes = input.blockHashes;
    ctx.inputs.accessList = input.accessList.get();
    ctx.inputs.authorizationListPresent = input.authorizationListPresent;
    ctx.inputs.authorizations = input.authorizations;
    ctx.inputs.web3TypedTxKind = input.web3TypedTxKind;

    trace::logMessageContext(input.message);

    // execBundle wires execution environment (vm, extension, chainPort) into ctx;
    // bindingsCtx is orchestration policy bind input only.
    FiscoExecutionBundle execBundle{ctx, input};

    FiscoStateTransitionBindings::Context bindingsCtx{
        input, output, fixErrorHandling, eip7623Enabled};
    auto bindings = FiscoStateTransitionBindings::bind(bindingsCtx);
    stateTransitionExecute(ctx, bindings.hooks, bindings.errorPolicy);

    EVM_LOG(DEBUG) << LOG_DESC("applyFiscoMessage done")
                   << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                   << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                   << LOG_KV("gasLeft", ctx.evmcResult.gas_left) << LOG_KV("web3Tx", input.web3Tx);

    output.evmcResult = std::move(ctx.evmcResult);
    output.executionContext.logs = convertLogs(ctx.kernelOutput.logs);
    output.executionContext.message = ctx.message;

    if (bindings.hooks.getIntrinsicGasParams().mode == IntrinsicDebitMode::Eip7623)
    {
        output.executionContext.gasSettlementSnapshot = ctx.snapshot;
    }

    if (output.evmcResult.status_code == EVMC_SUCCESS)
    {
        output.stateDiff = std::move(ctx.kernelOutput.stateDiff);
    }
    else if (input.revisionConfig.fix_revert_logs)
    {
        output.executionContext.logs.clear();
    }

    co_return output;
}

}  // namespace bcos::evm
