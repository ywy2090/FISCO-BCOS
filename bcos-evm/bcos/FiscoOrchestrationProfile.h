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
 * @file FiscoOrchestrationProfile.h
 */

#pragma once

#include "bcos-evm/bcos/FiscoExecutionBridge.h"
#include "bcos-evm/bcos/FiscoOrchestrationErrorPolicy.h"
#include "bcos-evm/bcos/FiscoPrecheckPolicy.h"
#include "bcos-evm/bcos/FiscoVmHostPolicy.h"

namespace bcos::evm
{

struct FiscoOrchestrationProfile
{
    struct Session
    {
        FiscoExecutionRequest const& input;
        FiscoExecutionResult& output;
        FiscoVmHostPolicy& extension;
        bool fixErrorHandling{false};
        bool eip7623Enabled{false};
    };

    struct Bindings
    {
        FiscoPrecheckPolicy precheckPolicy;
        FiscoOrchestrationErrorPolicy errorPolicy;
    };

    static FiscoPrecheckPolicy buildPrecheckPolicy(Session& session);
    static FiscoOrchestrationErrorPolicy buildErrorPolicy(Session const& session);
    static Bindings bind(Session& session);
};

}  // namespace bcos::evm
