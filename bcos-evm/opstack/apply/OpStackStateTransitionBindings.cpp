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
 * @file OpStackStateTransitionBindings.cpp
 */

#include "bcos-evm/opstack/apply/OpStackStateTransitionBindings.h"
#include "opstack/apply/ApplyOpStackMessage.h"
#include "opstack/apply/OpStackStateTransitionErrorPolicy.h"
#include "opstack/apply/OpStackStateTransitionHooks.h"

namespace bcos::evm
{

OpStackStateTransitionHooks OpStackStateTransitionBindings::buildStateTransitionHooks(Context& ctx)
{
    return OpStackStateTransitionHooks{ctx.view};
}

OpStackStateTransitionErrorPolicy OpStackStateTransitionBindings::buildErrorPolicy(
    Context const& /*ctx*/)
{
    return OpStackStateTransitionErrorPolicy{};
}

OpStackStateTransitionBindings::Result OpStackStateTransitionBindings::bind(Context& ctx)
{
    return Result{buildStateTransitionHooks(ctx), buildErrorPolicy(ctx)};
}

}  // namespace bcos::evm
