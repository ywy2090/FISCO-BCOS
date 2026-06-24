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
 * @file OpStackPipelineHookBinder.h
 */

#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/orchestration/OrchestrationHooks.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"

namespace bcos::evm
{

struct OpStackPipelineHookBinder
{
    struct HookBindingContext
    {
        OpStackExecutionRequest const& input;
        OpStackTxFeeLedger::OpStackTxExecutionData& txData;
    };

    static OrchestrationHooks buildHooks(HookBindingContext& session);
    static void applySettlement(HookBindingContext const& session, EVMCResult const& result);
};

}  // namespace bcos::evm
