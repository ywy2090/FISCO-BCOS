/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file FrameRouting.cpp
 */

#include "bcos-evm/eth/kernel/execution/FrameRouting.h"
#include "bcos-evm/eth/eip/Eip2929Gate.h"
#include "bcos-evm/eth/kernel/execution/FrameScope.h"
#include "bcos-evm/eth/state/State.hpp"
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

inline evmc_address pickExecutionAddressFromMessage(evmc_message const& msg) noexcept
{
    if (isDirectDelegated7702(msg))
    {
        return msg.recipient;
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
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(routed.routed.code_address);
            }
        }
        else if (!state::isZeroAddress(routed.routed.code_address))
        {
            routed.routed.recipient = routed.routed.code_address;
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(routed.routed.code_address);
            }
        }
    }

    if (scope == FrameScope::TopLevel)
    {
        if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
        {
            routed.executionAddress = pickExecutionAddressFromMessage(routed.routed);
            return routed;
        }
        if (state::isZeroAddress(routed.routed.code_address))
        {
            routed.routed.code_address = routed.routed.recipient;
        }
    }
    else
    {
        auto normalized = state::isZeroAddress(routed.routed.code_address) ?
                              routed.routed.recipient :
                              routed.routed.code_address;
        // EIP-7702: CALL/STATICCALL pass the authority as recipient; DELEGATECALL/CALLCODE keep
        // the resolved delegate in code_address. Delegation to a precompile runs empty code.
        if (isDirectDelegated7702(msg))
        {
            normalized = routed.routed.recipient;
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
