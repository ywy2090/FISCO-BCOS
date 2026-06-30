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
 * @brief Binds OpStack chain policy for `stateTransitionExecute` at `runOpStackTxLifecycle`.
 * @file OpStackStateTransitionBindings.h
 */

#pragma once

#include "bcos-evm/opstack/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "bcos-evm/opstack/OpStackStateTransitionErrorPolicy.h"
#include "bcos-evm/opstack/OpStackStateTransitionHooks.h"

namespace bcos::evm
{

/// Factory for `{hooks, errorPolicy}` injected into `stateTransitionExecute`.
struct OpStackStateTransitionBindings
{
    struct Context
    {
        OpStackExecutionRequest const& input;
        OpStackSettlementFacade view;
    };

    struct Result
    {
        OpStackStateTransitionHooks hooks;
        OpStackStateTransitionErrorPolicy errorPolicy;
    };

    static OpStackStateTransitionHooks buildStateTransitionHooks(Context& ctx);
    static OpStackStateTransitionErrorPolicy buildErrorPolicy(Context const& ctx);
    static Result bind(Context& ctx);
};

}  // namespace bcos::evm
