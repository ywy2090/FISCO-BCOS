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
 * @brief Binds FISCO chain policy for `stateTransitionExecute` at `applyFiscoMessage`.
 * @file FiscoStateTransitionBindings.h
 */

#pragma once

#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "bcos-evm/bcos/FiscoStateTransitionErrorPolicy.h"
#include "bcos-evm/bcos/FiscoStateTransitionHooks.h"

namespace bcos::evm
{

/// Factory for `{hooks, errorPolicy}` injected into `stateTransitionExecute`.
struct FiscoStateTransitionBindings
{
    struct Context
    {
        FiscoMessageRequest const& input;
        FiscoMessageResult& output;
        bool fixErrorHandling{false};
        bool eip7623Enabled{false};
    };

    struct Result
    {
        FiscoStateTransitionHooks hooks;
        FiscoStateTransitionErrorPolicy errorPolicy;
    };

    static FiscoStateTransitionHooks buildStateTransitionHooks(Context& ctx);
    static FiscoStateTransitionErrorPolicy buildErrorPolicy(Context const& ctx);
    static Result bind(Context& ctx);
};

}  // namespace bcos::evm
