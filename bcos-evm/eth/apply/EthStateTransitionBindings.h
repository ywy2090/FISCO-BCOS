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
 * @brief Binds Eth chain policy for `stateTransitionExecute` at `applyEthMessage`.
 * @file EthStateTransitionBindings.h
 */

#pragma once

#include "bcos-evm/eth/apply/EthStateTransitionErrorPolicy.h"
#include "bcos-evm/eth/apply/EthStateTransitionHooks.h"

namespace bcos::evm
{

struct EthMessageRequest;
struct EthMessageResult;

/// Factory for `{hooks, errorPolicy}` injected into `stateTransitionExecute`.
struct EthStateTransitionBindings
{
    struct Context
    {
        EthMessageRequest const& input;
        EthMessageResult& output;
    };

    struct Result
    {
        EthStateTransitionHooks hooks;
        EthStateTransitionErrorPolicy errorPolicy;
    };

    static EthStateTransitionHooks buildStateTransitionHooks(Context& ctx);
    static EthStateTransitionErrorPolicy buildErrorPolicy(Context const& ctx);
    static Result bind(Context& ctx);
};

}  // namespace bcos::evm
