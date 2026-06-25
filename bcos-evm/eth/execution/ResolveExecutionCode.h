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
 * @brief Resolve executable bytecode for an execution frame message.
 * @file ResolveExecutionCode.h
 */

#pragma once

#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/CreateContract.h"
#include "bcos-evm/eth/execution/Delegation7702Frame.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>

namespace bcos::evm::execution
{
inline bcos::bytes resolveExecutionCode(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message const& msg)
{
    if (isCreateKind(msg.kind))
    {
        if (msg.input_data == nullptr || msg.input_size == 0)
        {
            return {};
        }
        return bcos::bytes(msg.input_data, msg.input_data + msg.input_size);
    }

    auto const codeAddress = resolve7702CodeAddress(msg);
    if (!state::isZeroAddress(codeAddress) &&
        precompiled::isActivePrecompile(revisionConfig, codeAddress) &&
        state.get_code(codeAddress).empty())
    {
        return {};
    }

    auto code = state.get_code(codeAddress);
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
