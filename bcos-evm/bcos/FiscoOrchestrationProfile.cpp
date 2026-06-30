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
 * @file FiscoOrchestrationProfile.cpp
 */

#include "bcos-evm/bcos/FiscoOrchestrationProfile.h"

namespace bcos::evm
{

FiscoStateTransitionHooks FiscoOrchestrationProfile::buildStateTransitionHooks(
    BindingsContext& bindingsCtx)
{
    return FiscoStateTransitionHooks{bindingsCtx.input, bindingsCtx.eip7623Enabled};
}

FiscoStateTransitionErrorPolicy FiscoOrchestrationProfile::buildErrorPolicy(
    BindingsContext const& bindingsCtx)
{
    FiscoStateTransitionErrorPolicy errorPolicy;
    errorPolicy.hashImpl = bindingsCtx.input.hashImpl;
    errorPolicy.fixErrorHandling = bindingsCtx.fixErrorHandling;
    errorPolicy.fixRevertLogs = bindingsCtx.input.revisionConfig.fix_revert_logs;
    return errorPolicy;
}

FiscoOrchestrationProfile::Bindings FiscoOrchestrationProfile::bind(BindingsContext& bindingsCtx)
{
    return Bindings{buildStateTransitionHooks(bindingsCtx), buildErrorPolicy(bindingsCtx)};
}

}  // namespace bcos::evm
