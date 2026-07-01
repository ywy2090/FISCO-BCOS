/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file ExecutionAddressResolver.cpp
 */

#include "bcos-evm/eth/kernel/execution/ExecutionAddressResolver.h"
#include "bcos-evm/eth/eip/Eip2929Gate.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/execution/CreateContract.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{
namespace
{
inline bool isDirectDelegated7702(evmc_message const& msg) noexcept
{
    return (msg.flags & EVMC_DELEGATED) != 0 && msg.kind == EVMC_CALL &&
           (msg.flags & EVMC_STATIC) == 0;
}

inline evmc_address pickExecutionAddressFromMessage(evmc_message const& msg) noexcept
{
    if (isDirectDelegated7702(msg))
    {
        return msg.recipient;
    }
    return state::isZeroAddress(msg.code_address) ? msg.recipient : msg.code_address;
}
}  // namespace

ResolvedFrame resolveExecutionAddress(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message msg, FrameScope scope)
{
    ResolvedFrame resolved{};
    resolved.routed = msg;

    if (scope == FrameScope::Nested && isCreateKind(msg.kind))
    {
        if (!state::isZeroAddress(resolved.routed.recipient))
        {
            resolved.routed.code_address = resolved.routed.recipient;
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(resolved.routed.code_address);
            }
        }
        else if (!state::isZeroAddress(resolved.routed.code_address))
        {
            resolved.routed.recipient = resolved.routed.code_address;
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(resolved.routed.code_address);
            }
        }
    }

    if (scope == FrameScope::TopLevel)
    {
        if (isCreateKind(msg.kind))
        {
            resolved.executionAddress = pickExecutionAddressFromMessage(resolved.routed);
            return resolved;
        }
        if (state::isZeroAddress(resolved.routed.code_address))
        {
            resolved.routed.code_address = resolved.routed.recipient;
        }
    }
    else
    {
        auto normalized = state::isZeroAddress(resolved.routed.code_address) ?
                              resolved.routed.recipient :
                              resolved.routed.code_address;
        // EIP-7702: CALL/STATICCALL pass the authority as recipient; DELEGATECALL/CALLCODE keep
        // the resolved delegate in code_address. Delegation to a precompile runs empty code.
        if (isDirectDelegated7702(msg))
        {
            normalized = resolved.routed.recipient;
        }
        if (!state::isZeroAddress(normalized))
        {
            resolved.routed.code_address = normalized;
            if (state::isZeroAddress(resolved.routed.recipient))
            {
                resolved.routed.recipient = normalized;
            }
        }
    }

    resolved.executionAddress = pickExecutionAddressFromMessage(resolved.routed);
    return resolved;
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
