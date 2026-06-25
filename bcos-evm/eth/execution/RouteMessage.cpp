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
 * @file RouteMessage.cpp
 */

#include "bcos-evm/eth/execution/RouteMessage.h"
#include "bcos-evm/eth/execution/CreateContract.h"
#include "bcos-evm/eth/execution/Delegation7702Frame.h"
#include "bcos-evm/eth/execution/Eip2929Access.h"
#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm::execution
{
RoutedMessage routeMessage(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message msg, FrameScope scope)
{
    RoutedMessage routed{};
    routed.message = msg;

    if (scope == FrameScope::Nested && isCreateKind(msg.kind))
    {
        if (!state::isZeroAddress(routed.message.recipient))
        {
            routed.message.code_address = routed.message.recipient;
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(routed.message.code_address);
            }
        }
        else if (!state::isZeroAddress(routed.message.code_address))
        {
            routed.message.recipient = routed.message.code_address;
            if (isCreateWarmPinEnabled(revisionConfig))
            {
                state.pin_warm_create_address(routed.message.code_address);
            }
        }
    }

    if (scope == FrameScope::TopLevel)
    {
        if (isCreateKind(msg.kind))
        {
            return routed;
        }
        if (state::isZeroAddress(routed.message.code_address))
        {
            routed.message.code_address = routed.message.recipient;
        }
    }
    else
    {
        auto target = state::isZeroAddress(routed.message.code_address) ?
                          routed.message.recipient :
                          routed.message.code_address;
        // EIP-7702: CALL/STATICCALL pass the authority as recipient; DELEGATECALL/CALLCODE keep
        // the resolved delegate in code_address. Delegation to a precompile runs empty code.
        if (isDirectDelegated7702(msg))
        {
            target = routed.message.recipient;
        }
        if (!state::isZeroAddress(target))
        {
            routed.message.code_address = target;
            if (state::isZeroAddress(routed.message.recipient))
            {
                routed.message.recipient = target;
            }
        }
    }

    return routed;
}
}  // namespace bcos::evm::execution
