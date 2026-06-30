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
 * @file FiscoOrchestrationProfile.h
 */

#pragma once

#include "bcos-evm/bcos/ApplyFiscoMessage.h"
#include "bcos-evm/bcos/FiscoStateTransitionErrorPolicy.h"
#include "bcos-evm/bcos/FiscoStateTransitionHooks.h"

namespace bcos::evm
{

struct FiscoOrchestrationProfile
{
    /// Orchestration policy bind input (not execution environment wiring).
    struct BindingsContext
    {
        FiscoExecutionRequest const& input;
        FiscoExecutionResult& output;
        bool fixErrorHandling{false};
        bool eip7623Enabled{false};
    };

    struct Bindings
    {
        FiscoStateTransitionHooks hooks;
        FiscoStateTransitionErrorPolicy errorPolicy;
    };

    static FiscoStateTransitionHooks buildStateTransitionHooks(BindingsContext& bindingsCtx);
    static FiscoStateTransitionErrorPolicy buildErrorPolicy(BindingsContext const& bindingsCtx);
    static Bindings bind(BindingsContext& bindingsCtx);
};

}  // namespace bcos::evm
