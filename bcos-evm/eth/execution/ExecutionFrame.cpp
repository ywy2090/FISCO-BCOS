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
#include "bcos-evm/eth/execution/CreateContract.h"
#include "bcos-evm/eth/execution/Delegation7702Frame.h"
#include "bcos-evm/eth/execution/Eip2929Access.h"
#include "bcos-evm/eth/execution/FrameCaller.h"
#include "bcos-evm/eth/execution/FrameValueTransfer.h"
#include "bcos-evm/eth/execution/ResolveExecutionCode.h"
#include "bcos-evm/eth/execution/RouteMessage.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include <cstring>
#include <optional>

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

struct FrameWork
{
    FrameContext& ctx;
    evmc_message const& originalMsg;
    RoutedMessage routed;
    bcos::bytes code;
    state::EthHost& host;

    evmc_message& callMessage() noexcept { return routed.message; }
    evmc_message const& callMessage() const noexcept { return routed.message; }
};

void logFrameDoneIfNested(evmc_message const& originalMsg, evmc::Result const& result)
{
    if (originalMsg.depth > 0)
    {
        EVM_LOG(TRACE) << LOG_DESC("EthHost::call done") << LOG_KV("depth", originalMsg.depth)
                       << LOG_KV("status", trace::evmcStatus(result.status_code))
                       << LOG_KV("gasLeft", result.gas_left);
    }
}

inline evmc_address resolveCallerAddress(
    evmc_address const& executionAddress, evmc_message const& msg) noexcept
{
    if (!state::isZeroAddress(executionAddress))
    {
        return executionAddress;
    }
    return msg.sender;
}

std::optional<FrameResult> guardDelegatePrecompile(FrameWork const& work)
{
    auto& callMessage = work.callMessage();
    if (callMessage.kind == EVMC_DELEGATECALL && work.routed.hasPrecompileTarget &&
        work.ctx.extension != nullptr && !work.ctx.extension->allowDelegateCallToPrecompile())
    {
        return FrameResult{.result = makeFrameResult(EVMC_PRECOMPILE_FAILURE, callMessage.gas)};
    }
    return std::nullopt;
}

std::optional<FrameResult> tryPrecompileAtTarget(FrameWork& work, evmc_address const& target)
{
    auto& callMessage = work.callMessage();
    if (isCreateKind(callMessage.kind))
    {
        return std::nullopt;
    }
    if (isDelegated7702Message(work.originalMsg) && callMessage.kind != EVMC_CALL)
    {
        return std::nullopt;
    }
    bool const skipVt =
        work.ctx.extension != nullptr && work.ctx.extension->skipHostValueTransfer();
    auto out = precompiled::dispatchPrecompile(
        {work.ctx.state, work.ctx.revisionConfig, work.ctx.extension, callMessage, target, skipVt});
    if (out.outcome == precompiled::PrecompileDispatchOutcome::NotApplicable)
    {
        return std::nullopt;
    }
    return FrameResult{
        .result = std::move(out.result), .gasRefund = out.gasRefund, .precompileHit = true};
}

std::optional<FrameResult> transferOrFail(FrameWork& work, FrameScope scope)
{
    if (!transferFrameValue(
            work.ctx.state, work.ctx.revisionConfig, work.ctx.extension, work.callMessage(), scope))
    {
        work.ctx.state.revert();
        return FrameResult{.result = makeFrameResult(EVMC_INSUFFICIENT_BALANCE, 0)};
    }
    return std::nullopt;
}

void prepareNestedMessage(FrameWork& work)
{
    auto const callerAddress = resolveCallerAddress(work.ctx.executionAddress, work.routed.message);
    if (work.ctx.extension != nullptr)
    {
        work.ctx.extension->setCallerAddress(callerAddress);
        work.ctx.extension->prepareMessage(work.ctx.revisionConfig.revision, work.routed.message);
    }
}

void bindCreateForInit(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    bindCreateMessageForInit(work.host, callMessage,
        bcos::bytesConstRef(callMessage.input_data, callMessage.input_size), work.ctx.state);
}

void checkpointFrame(FrameWork& work)
{
    work.ctx.state.checkpoint();
}

void initializeCreateAccount(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    initializeCreateTargetAccount(work.ctx.state, callMessage.recipient,
        work.ctx.revisionConfig.revision, isCreateWarmPinEnabled(work.ctx.revisionConfig));
}

evmc::Result runVm(FrameWork& work)
{
    auto& callMessage = work.callMessage();
    if (work.code.empty())
    {
        work.code = resolveExecutionCode(work.ctx.state, work.ctx.revisionConfig, callMessage);
    }
    return work.ctx.vm.execute(work.host, work.ctx.revisionConfig.revision, callMessage,
        work.code.data(), work.code.size());
}

FrameResult finalizeFrame(FrameWork& work, FrameScope scope, evmc::Result result)
{
    auto& callMessage = work.callMessage();

    if (result.status_code == EVMC_SUCCESS && isCreateKind(callMessage.kind))
    {
        auto raw = result.release_raw();
        if (!applyCreateCodeDepositGas(raw, work.ctx.revisionConfig.revision) &&
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
        state::installCreatedContractCode(work.ctx.state, callMessage, result.raw());
        if (isCreateKind(callMessage.kind))
        {
            evmc_address createAddr = scope == FrameScope::TopLevel ?
                                          resolveCreateAddress(callMessage, result.raw()) :
                                          callMessage.recipient;
            work.host.markCreatedInTx(createAddr);
            auto& raw = const_cast<evmc_result&>(result.raw());
            if (state::isZeroAddress(raw.create_address))
            {
                raw.create_address = callMessage.recipient;
            }
        }

        if (scope == FrameScope::TopLevel)
        {
            if (work.ctx.fixNonceInit && isCreateKind(callMessage.kind))
            {
                auto createAddr = resolveCreateAddress(callMessage, result.raw());
                if (!state::isZeroAddress(createAddr))
                {
                    work.ctx.state.set_nonce(createAddr, 1);
                }
            }
        }
        else
        {
            work.ctx.state.commit();
            if (!isCreateKind(callMessage.kind))
            {
                auto const nextExecution = state::isZeroAddress(callMessage.code_address) ?
                                               callMessage.recipient :
                                               callMessage.code_address;
                if (!state::isZeroAddress(nextExecution))
                {
                    work.ctx.executionAddress = nextExecution;
                }
            }
        }
    }
    else
    {
        work.ctx.state.revert();
    }

    logFrameDoneIfNested(work.originalMsg, result);
    return FrameResult{.result = std::move(result)};
}

FrameResult runTopLevelSteps(FrameContext& ctx, evmc_message message, state::EthHost& host)
{
    FrameWork work{ctx, message,
        routeMessage(ctx.state, ctx.revisionConfig, message, FrameScope::TopLevel), {}, host};

    if (auto early = guardDelegatePrecompile(work))
    {
        return std::move(*early);
    }

    work.code = resolveExecutionCode(ctx.state, ctx.revisionConfig, work.callMessage());
    if (work.code.empty())
    {
        auto& callMessage = work.callMessage();
        auto const target =
            work.routed.hasPrecompileTarget ?
                work.routed.precompileTarget :
                (state::isZeroAddress(callMessage.code_address) ? callMessage.recipient :
                                                                  callMessage.code_address);
        if (auto early = tryPrecompileAtTarget(work, target))
        {
            return std::move(*early);
        }
    }

    checkpointFrame(work);

    if (!isCreateKind(work.callMessage().kind))
    {
        if (auto early = transferOrFail(work, FrameScope::TopLevel))
        {
            return std::move(*early);
        }
    }
    else
    {
        // RR7 TopLevel: checkpoint → bindCreate → transfer → initAccount
        bindCreateForInit(work);
        if (auto early = transferOrFail(work, FrameScope::TopLevel))
        {
            return std::move(*early);
        }
        initializeCreateAccount(work);
    }

    auto result = runVm(work);
    return finalizeFrame(work, FrameScope::TopLevel, std::move(result));
}

FrameResult runNestedSteps(FrameContext& ctx, evmc_message message, state::EthHost& host)
{
    FrameWork work{ctx, message,
        routeMessage(ctx.state, ctx.revisionConfig, message, FrameScope::Nested), {}, host};

    if (auto early = guardDelegatePrecompile(work))
    {
        return std::move(*early);
    }

    // RR6: tryPrecompile before prepareNestedMessage
    {
        auto& callMessage = work.callMessage();
        auto const target = state::isZeroAddress(callMessage.code_address) ?
                                callMessage.recipient :
                                callMessage.code_address;
        if (auto early = tryPrecompileAtTarget(work, target))
        {
            return std::move(*early);
        }
    }

    prepareNestedMessage(work);

    if (isCreateKind(work.callMessage().kind))
    {
        bindCreateForInit(work);
    }

    checkpointFrame(work);

    if (isCreateKind(work.callMessage().kind))
    {
        initializeCreateAccount(work);
    }

    if (auto early = transferOrFail(work, FrameScope::Nested))
    {
        return std::move(*early);
    }

    auto result = runVm(work);
    auto fr = finalizeFrame(work, FrameScope::Nested, std::move(result));

    auto& callMessage = work.callMessage();
    if (isCreateKind(callMessage.kind) && !state::isZeroAddress(callMessage.sender) &&
        work.originalMsg.depth > 0)
    {
        ctx.state.set_nonce(callMessage.sender, ctx.state.get_nonce(callMessage.sender) + 1);
        if (ctx.extension != nullptr && std::memcmp(callMessage.sender.bytes, ctx.txOrigin.bytes,
                                            sizeof(callMessage.sender.bytes)) != 0)
        {
            ctx.extension->bumpContractCreateNonce(callMessage.sender);
        }
    }

    return fr;
}
}  // namespace

FrameResult runExecutionFrame(
    FrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host)
{
    if (scope == FrameScope::TopLevel)
    {
        return runTopLevelSteps(ctx, message, host);
    }
    return runNestedSteps(ctx, message, host);
}

}  // namespace bcos::evm::execution
