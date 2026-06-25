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
 * @brief EIP-7702 delegation helpers shared by RouteMessage and ExecutionFrame.
 * @file Delegation7702Frame.h
 */

#pragma once

#include "bcos-evm/eth/state/HashUtils.hpp"
#include <evmc/evmc.h>

namespace bcos::evm::execution
{
// CALL/STATICCALL: recipient is the authority. DELEGATECALL/CALLCODE: recipient is the caller
// context and evmone puts the resolved delegate in code_address.
inline bool isDirectDelegated7702(evmc_message const& msg) noexcept
{
    return (msg.flags & EVMC_DELEGATED) != 0 && msg.kind == EVMC_CALL;
}

inline bool isDelegated7702Message(evmc_message const& msg) noexcept
{
    return (msg.flags & EVMC_DELEGATED) != 0;
}

// Account whose bytecode is resolved for VM execution and precompile dispatch.
inline evmc_address resolve7702CodeAddress(evmc_message const& msg) noexcept
{
    if (isDirectDelegated7702(msg))
    {
        return msg.recipient;
    }
    return state::isZeroAddress(msg.code_address) ? msg.recipient : msg.code_address;
}
}  // namespace bcos::evm::execution
