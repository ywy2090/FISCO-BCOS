/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file FrameRouting.cpp
 */

#include "bcos-evm/eth/kernel/execution/FrameRouting.h"
#include "bcos-evm/eth/kernel/execution/FrameScope.h"
#include "bcos-evm/eth/state/State.hpp"
#include "eth/RevisionConfig.h"
#include "eth/state/StateKeyHash.hpp"

namespace bcos::evm::execution
{
namespace
{
inline bool isDirectDelegated7702(evmc_message const& msg) noexcept
{
    return (msg.flags & EVMC_DELEGATED) != 0 && msg.kind == EVMC_CALL &&
           (msg.flags & EVMC_STATIC) == 0;
}

inline bool isCallcodeFamily(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CALLCODE || kind == EVMC_DELEGATECALL;
}

inline evmc_address pickExecutionAddressFromMessage(evmc_message const& msg) noexcept
{
    if (isDirectDelegated7702(msg))
    {
        return msg.recipient;
    }
    // geth: DELEGATECALL/CALLCODE with zero code address runs empty bytecode in the
    // caller's storage/balance context — do not substitute recipient (re-enters caller code).
    if (isCallcodeFamily(msg.kind))
    {
        return msg.code_address;
    }
    return state::isZeroAddress(msg.code_address) ? msg.recipient : msg.code_address;
}
}  // namespace

RoutedFrame routeFrameMessage(state::State& state, bcos::evm::RevisionConfig const& revisionConfig,
    evmc_message msg, FrameScope scope)
{
    RoutedFrame routed{};
    routed.routed = msg;

    if (scope == FrameScope::Nested && (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2))
    {
        if (!state::isZeroAddress(routed.routed.recipient))
        {
            routed.routed.code_address = routed.routed.recipient;
        }
        else if (!state::isZeroAddress(routed.routed.code_address))
        {
            routed.routed.recipient = routed.routed.code_address;
        }
    }

    if (scope == FrameScope::TopLevel)
    {
        if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
        {
            routed.executionAddress = pickExecutionAddressFromMessage(routed.routed);
            return routed;
        }
        if (state::isZeroAddress(routed.routed.code_address) && !isCallcodeFamily(msg.kind))
        {
            routed.routed.code_address = routed.routed.recipient;
        }
    }
    else
    {
        evmc_address normalized{};
        // EIP-7702: CALL/STATICCALL pass the authority as recipient; DELEGATECALL/CALLCODE keep
        // the resolved delegate in code_address. Delegation to a precompile runs empty code.
        if (isDirectDelegated7702(msg))
        {
            normalized = routed.routed.recipient;
        }
        else if (isCallcodeFamily(msg.kind))
        {
            normalized = routed.routed.code_address;
        }
        else
        {
            normalized = state::isZeroAddress(routed.routed.code_address) ?
                             routed.routed.recipient :
                             routed.routed.code_address;
        }
        if (!state::isZeroAddress(normalized))
        {
            routed.routed.code_address = normalized;
            if (state::isZeroAddress(routed.routed.recipient))
            {
                routed.routed.recipient = normalized;
            }
        }
    }

    routed.executionAddress = pickExecutionAddressFromMessage(routed.routed);
    return routed;
}

}  // namespace bcos::evm::execution
