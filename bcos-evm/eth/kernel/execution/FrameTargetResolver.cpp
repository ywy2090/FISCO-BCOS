/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file FrameTargetResolver.cpp
 */

#include "bcos-evm/eth/kernel/execution/FrameTargetResolver.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/execution/CreateContract.h"
#include "bcos-evm/eth/kernel/execution/Eip2929Access.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{
namespace
{
// EIP-7702 delegation helpers (internal to frame target resolution).
inline bool isDirectDelegated7702(evmc_message const& msg) noexcept
{
    return (msg.flags & EVMC_DELEGATED) != 0 && msg.kind == EVMC_CALL &&
           (msg.flags & EVMC_STATIC) == 0;
}

inline evmc_address resolveExecutionAddress(evmc_message const& msg) noexcept
{
    if (isDirectDelegated7702(msg))
    {
        return msg.recipient;
    }
    return state::isZeroAddress(msg.code_address) ? msg.recipient : msg.code_address;
}
}  // namespace

FrameTarget resolveFrameTarget(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message msg, FrameScope scope)
{
    FrameTarget target{};
    target.routed = msg;

    if (scope == FrameScope::Nested && isCreateKind(msg.kind))
    {
        if (!state::isZeroAddress(target.routed.recipient))
        {
            target.routed.code_address = target.routed.recipient;
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(target.routed.code_address);
            }
        }
        else if (!state::isZeroAddress(target.routed.code_address))
        {
            target.routed.recipient = target.routed.code_address;
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(target.routed.code_address);
            }
        }
    }

    if (scope == FrameScope::TopLevel)
    {
        if (isCreateKind(msg.kind))
        {
            target.executionAddress = resolveExecutionAddress(target.routed);
            return target;
        }
        if (state::isZeroAddress(target.routed.code_address))
        {
            target.routed.code_address = target.routed.recipient;
        }
    }
    else
    {
        auto resolved = state::isZeroAddress(target.routed.code_address) ?
                            target.routed.recipient :
                            target.routed.code_address;
        // EIP-7702: CALL/STATICCALL pass the authority as recipient; DELEGATECALL/CALLCODE keep
        // the resolved delegate in code_address. Delegation to a precompile runs empty code.
        if (isDirectDelegated7702(msg))
        {
            resolved = target.routed.recipient;
        }
        if (!state::isZeroAddress(resolved))
        {
            target.routed.code_address = resolved;
            if (state::isZeroAddress(target.routed.recipient))
            {
                target.routed.recipient = resolved;
            }
        }
    }

    target.executionAddress = resolveExecutionAddress(target.routed);
    return target;
}

bcos::bytes resolveExecutionCode(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message const& msg,
    evmc_address executionAddress)
{
    if (isCreateKind(msg.kind))
    {
        if (msg.input_data == nullptr || msg.input_size == 0)
        {
            return {};
        }
        return bcos::bytes(msg.input_data, msg.input_data + msg.input_size);
    }

    auto code = state.get_code(executionAddress);
    if (revisionConfig.eip7702)
    {
        if (auto const delegate =
                parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
        {
            return state.get_code(*delegate);
        }
    }
    return code;
}

}  // namespace bcos::evm::execution
