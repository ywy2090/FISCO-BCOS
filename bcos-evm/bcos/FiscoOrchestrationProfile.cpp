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

FiscoPrecheckPolicy FiscoOrchestrationProfile::buildPrecheckPolicy(Session& session)
{
    return FiscoPrecheckPolicy{session.input, session.eip7623Enabled};
}

FiscoOrchestrationErrorPolicy FiscoOrchestrationProfile::buildErrorPolicy(Session const& session)
{
    FiscoOrchestrationErrorPolicy errorPolicy;
    errorPolicy.hashImpl = session.input.hashImpl;
    errorPolicy.fixErrorHandling = session.fixErrorHandling;
    errorPolicy.fixRevertLogs = session.input.revisionConfig.fix_revert_logs;
    return errorPolicy;
}

FiscoOrchestrationProfile::Bindings FiscoOrchestrationProfile::bind(Session& session)
{
    return Bindings{buildPrecheckPolicy(session), buildErrorPolicy(session)};
}

}  // namespace bcos::evm
