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
 * @brief Message routing for execution frames (7702, CREATE fill, precompile target).
 * @file RouteMessage.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include <evmc/evmc.h>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{
struct RoutedMessage
{
    evmc_message message{};
    evmc_address precompileTarget{};
    bool hasPrecompileTarget{false};
};

RoutedMessage routeMessage(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message msg, FrameScope scope);
}  // namespace bcos::evm::execution
