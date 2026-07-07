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
 * @file EthStateTransitionBindings.cpp
 */

#include "bcos-evm/eth/apply/EthStateTransitionBindings.h"
#include "eth/apply/ApplyEthMessage.h"
#include "eth/apply/EthStateTransitionErrorPolicy.h"
#include "eth/apply/EthStateTransitionHooks.h"

namespace bcos::evm
{

EthStateTransitionHooks EthStateTransitionBindings::buildStateTransitionHooks(Context& ctx)
{
    return EthStateTransitionHooks{ctx.input};
}

EthStateTransitionErrorPolicy EthStateTransitionBindings::buildErrorPolicy(Context const& ctx)
{
    EthStateTransitionErrorPolicy policy;
    policy.isCall = ctx.input.isCall;
    return policy;
}

EthStateTransitionBindings::Result EthStateTransitionBindings::bind(Context& ctx)
{
    return Result{buildStateTransitionHooks(ctx), buildErrorPolicy(ctx)};
}

}  // namespace bcos::evm
