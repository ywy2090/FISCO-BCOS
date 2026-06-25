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
 * @file ExecutionFrame.cpp
 */

#include "bcos-evm/eth/execution/ExecutionFrame.h"
#include "bcos-evm/eth/execution/Delegation7702Frame.h"
#include "bcos-evm/eth/execution/FrameCaller.h"
#include "bcos-evm/eth/execution/FrameValueTransfer.h"
#include "bcos-evm/eth/execution/ResolveExecutionCode.h"
#include "bcos-evm/eth/execution/RouteMessage.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "bcos-evm/eth/state/CreateExecution.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <cstring>

namespace bcos::evm::execution
{
namespace
{
evmc::Result makeFrameResult(evmc_status_code status, int64_t gasLeft)
{
    evmc_result result{};
    result.status_code = status;
    result.gas_left = gasLeft;
    result.gas_refund = 0;
    result.create_address = {};
    return evmc::Result(result);
}

evmc_address resolveCreateAddress(evmc_message const& message, evmc_result const& result) noexcept
{
    auto createAddr = message.recipient;
    if (state::isZeroAddress(createAddr))
    {
        createAddr = message.code_address;
    }
    if (state::isZeroAddress(createAddr))
    {
        createAddr = result.create_address;
    }
    return createAddr;
}

FrameResult runTopLevelExecutionFrame(FrameContext& ctx, evmc_message message, state::EthHost& host)
{
    auto const originalMsg = message;
    auto routed = routeMessage(ctx.state, ctx.revisionConfig, message, FrameScope::TopLevel);
    auto& callMessage = routed.message;

    if (callMessage.kind == EVMC_DELEGATECALL && routed.hasPrecompileTarget &&
        ctx.extension != nullptr && !ctx.extension->allowDelegateCallToPrecompile())
    {
        return FrameResult{.result = makeFrameResult(EVMC_PRECOMPILE_FAILURE, callMessage.gas)};
    }

    auto code = resolveExecutionCode(ctx.state, ctx.revisionConfig, callMessage);
    if (!state::isCreateKind(callMessage.kind) &&
        !(isDelegated7702Message(originalMsg) && callMessage.kind != EVMC_CALL) && code.empty())
    {
        bool const skipVt = ctx.extension != nullptr && ctx.extension->skipHostValueTransfer();
        auto const target =
            routed.hasPrecompileTarget ?
                routed.precompileTarget :
                (state::isZeroAddress(callMessage.code_address) ? callMessage.recipient :
                                                                  callMessage.code_address);
        auto out = precompiled::dispatchPrecompile(
            {ctx.state, ctx.revisionConfig, ctx.extension, callMessage, target, skipVt});
        if (out.outcome != precompiled::PrecompileDispatchOutcome::NotApplicable)
        {
            return FrameResult{
                .result = std::move(out.result), .gasRefund = out.gasRefund, .precompileHit = true};
        }
    }

    ctx.state.checkpoint();
    if (!state::isCreateKind(callMessage.kind))
    {
        if (!transferFrameValue(
                ctx.state, ctx.revisionConfig, ctx.extension, callMessage, FrameScope::TopLevel))
        {
            ctx.state.revert();
            return FrameResult{.result = makeFrameResult(EVMC_INSUFFICIENT_BALANCE, 0)};
        }
    }
    else
    {
        state::bindCreateMessageForInit(host, callMessage,
            bcos::bytesConstRef(callMessage.input_data, callMessage.input_size), ctx.state);
        if (!transferFrameValue(
                ctx.state, ctx.revisionConfig, ctx.extension, callMessage, FrameScope::TopLevel))
        {
            ctx.state.revert();
            return FrameResult{.result = makeFrameResult(EVMC_INSUFFICIENT_BALANCE, 0)};
        }
        state::initializeCreateTargetAccount(ctx.state, callMessage.recipient,
            ctx.revisionConfig.revision, ctx.revisionConfig.warm_access);
    }

    auto result =
        ctx.vm.execute(host, ctx.revisionConfig.revision, callMessage, code.data(), code.size());
    if (result.status_code == EVMC_SUCCESS && state::isCreateKind(callMessage.kind))
    {
        auto raw = result.release_raw();
        if (!state::applyCreateCodeDepositGas(raw, ctx.revisionConfig.revision) &&
            raw.release != nullptr)
        {
            raw.release(&raw);
            raw.release = nullptr;
            raw.output_data = nullptr;
            raw.output_size = 0;
        }
        if (raw.status_code == EVMC_SUCCESS)
        {
            result = evmc::Result(raw);
        }
        else
        {
            result = makeFrameResult(raw.status_code, raw.gas_left);
        }
    }
    if (result.status_code == EVMC_SUCCESS)
    {
        state::installCreatedContractCode(ctx.state, callMessage, result.raw());
        if (state::isCreateKind(callMessage.kind))
        {
            auto createAddr = resolveCreateAddress(callMessage, result.raw());
            host.markCreatedInTx(createAddr);
            auto& raw = const_cast<evmc_result&>(result.raw());
            if (state::isZeroAddress(raw.create_address))
            {
                raw.create_address = callMessage.recipient;
            }
        }
        if (ctx.fixNonceInit && state::isCreateKind(callMessage.kind))
        {
            auto createAddr = resolveCreateAddress(callMessage, result.raw());
            if (!state::isZeroAddress(createAddr))
            {
                ctx.state.set_nonce(createAddr, 1);
            }
        }
    }
    else
    {
        ctx.state.revert();
    }

    if (originalMsg.depth > 0)
    {
        EVM_LOG(TRACE) << LOG_DESC("EthHost::call done") << LOG_KV("depth", originalMsg.depth)
                       << LOG_KV("status", trace::evmcStatus(result.status_code))
                       << LOG_KV("gasLeft", result.gas_left);
    }

    return FrameResult{.result = std::move(result)};
}
}  // namespace

FrameResult runExecutionFrame(
    FrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    if (scope == FrameScope::TopLevel)
    {
        return runTopLevelExecutionFrame(ctx, message, host);
    }

    auto const originalMsg = message;
    auto routed = routeMessage(ctx.state, ctx.revisionConfig, message, FrameScope::Nested);
    auto& callMessage = routed.message;

    if (callMessage.kind == EVMC_DELEGATECALL && routed.hasPrecompileTarget &&
        ctx.extension != nullptr && !ctx.extension->allowDelegateCallToPrecompile())
    {
        return FrameResult{.result = makeFrameResult(EVMC_PRECOMPILE_FAILURE, callMessage.gas)};
    }

    if (!state::isCreateKind(callMessage.kind) &&
        !(isDelegated7702Message(originalMsg) && callMessage.kind != EVMC_CALL))
    {
        bool const skipVt = ctx.extension != nullptr && ctx.extension->skipHostValueTransfer();
        auto const target = state::isZeroAddress(callMessage.code_address) ?
                                callMessage.recipient :
                                callMessage.code_address;
        auto out = precompiled::dispatchPrecompile(
            {ctx.state, ctx.revisionConfig, ctx.extension, callMessage, target, skipVt});
        if (out.outcome != precompiled::PrecompileDispatchOutcome::NotApplicable)
        {
            return FrameResult{
                .result = std::move(out.result), .gasRefund = out.gasRefund, .precompileHit = true};
        }
    }

    auto const callerAddress = resolveCallerAddress(ctx.executionAddress, routed.message);
    if (ctx.extension != nullptr)
    {
        ctx.extension->setCallerAddress(callerAddress);
        ctx.extension->prepareMessage(ctx.revisionConfig.revision, routed.message);
    }

    if (state::isCreateKind(callMessage.kind))
    {
        state::bindCreateMessageForInit(host, callMessage,
            bcos::bytesConstRef(callMessage.input_data, callMessage.input_size), ctx.state);
    }

    ctx.state.checkpoint();
    if (state::isCreateKind(callMessage.kind))
    {
        state::initializeCreateTargetAccount(ctx.state, callMessage.recipient,
            ctx.revisionConfig.revision, ctx.revisionConfig.warm_access);
    }

    if (!transferFrameValue(
            ctx.state, ctx.revisionConfig, ctx.extension, callMessage, FrameScope::Nested))
    {
        ctx.state.revert();
        return FrameResult{.result = makeFrameResult(EVMC_INSUFFICIENT_BALANCE, 0)};
    }

    auto code = resolveExecutionCode(ctx.state, ctx.revisionConfig, callMessage);
    auto result =
        ctx.vm.execute(host, ctx.revisionConfig.revision, callMessage, code.data(), code.size());
    if (result.status_code == EVMC_SUCCESS && state::isCreateKind(callMessage.kind))
    {
        auto raw = result.release_raw();
        if (!state::applyCreateCodeDepositGas(raw, ctx.revisionConfig.revision) &&
            raw.release != nullptr)
        {
            raw.release(&raw);
            raw.release = nullptr;
            raw.output_data = nullptr;
            raw.output_size = 0;
        }
        if (raw.status_code == EVMC_SUCCESS)
        {
            result = evmc::Result(raw);
        }
        else
        {
            result = makeFrameResult(raw.status_code, raw.gas_left);
        }
    }
    if (result.status_code == EVMC_SUCCESS)
    {
        state::installCreatedContractCode(ctx.state, callMessage, result.raw());
        if (state::isCreateKind(callMessage.kind))
        {
            auto createAddr = callMessage.recipient;
            host.markCreatedInTx(createAddr);
            auto& raw = const_cast<evmc_result&>(result.raw());
            if (state::isZeroAddress(raw.create_address))
            {
                raw.create_address = createAddr;
            }
        }
        ctx.state.commit();
        if (!state::isCreateKind(callMessage.kind))
        {
            auto const nextExecution = state::isZeroAddress(callMessage.code_address) ?
                                           callMessage.recipient :
                                           callMessage.code_address;
            if (!state::isZeroAddress(nextExecution))
            {
                ctx.executionAddress = nextExecution;
            }
        }
    }
    else
    {
        ctx.state.revert();
    }

    if (originalMsg.depth > 0)
    {
        EVM_LOG(TRACE) << LOG_DESC("EthHost::call done") << LOG_KV("depth", originalMsg.depth)
                       << LOG_KV("status", trace::evmcStatus(result.status_code))
                       << LOG_KV("gasLeft", result.gas_left);
    }

    if (state::isCreateKind(callMessage.kind) && !state::isZeroAddress(callMessage.sender) &&
        originalMsg.depth > 0)
    {
        ctx.state.set_nonce(callMessage.sender, ctx.state.get_nonce(callMessage.sender) + 1);
        if (ctx.extension != nullptr && std::memcmp(callMessage.sender.bytes, ctx.txOrigin.bytes,
                                            sizeof(callMessage.sender.bytes)) != 0)
        {
            ctx.extension->bumpContractCreateNonce(callMessage.sender);
        }
    }

    return FrameResult{.result = std::move(result)};
}

}  // namespace bcos::evm::execution
