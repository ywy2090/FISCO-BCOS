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
 * @brief Scope-aware value transfer for execution frames.
 * @file FrameValueTransfer.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/core/FrameScope.h"
#include "bcos-evm/eth/kernel/execution/CreateContract.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>

namespace bcos::evm::execution
{
namespace
{
evmc_address resolveCodeAddress(evmc_message const& message) noexcept
{
    auto codeAddress = message.code_address;
    if (state::isZeroAddress(codeAddress))
    {
        codeAddress = message.recipient;
    }
    return codeAddress;
}
}  // namespace

inline bool transferFrameValue(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, state::EvmHostHooks* extension,
    evmc_message const& msg, FrameScope scope) noexcept
{
    (void)revisionConfig;

    if (state::isZeroBytes32(msg.value))
    {
        return true;
    }
    if (extension != nullptr && extension->skipHostValueTransfer())
    {
        return true;
    }

    auto const value = state::fromEvmC(msg.value);

    if (scope == FrameScope::TopLevel)
    {
        if (isCreateKind(msg.kind))
        {
            if (state.get_balance(msg.sender) < value)
            {
                return false;
            }
            state.set_balance(msg.sender, state.get_balance(msg.sender) - value);
            state.set_balance(msg.recipient, state.get_balance(msg.recipient) + value);
            return true;
        }

        auto const recipient = resolveCodeAddress(msg);
        if (state.get_balance(msg.sender) < value)
        {
            return false;
        }
        state.set_balance(msg.sender, state.get_balance(msg.sender) - value);
        state.set_balance(recipient, state.get_balance(recipient) + value);
        return true;
    }

    if (state.get_balance(msg.sender) < value)
    {
        return false;
    }
    state.set_balance(msg.sender, state.get_balance(msg.sender) - value);
    state.set_balance(msg.recipient, state.get_balance(msg.recipient) + value);
    return true;
}
}  // namespace bcos::evm::execution
