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
 * @file StateTransitionContext.cpp
 *
 * Bridges StateTransitionContext to InnerExecuteInput for the EVM entry step.
 */

#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"

namespace bcos::evm
{

/// Flatten context fields into the innerExecute input bundle.
/// state is const_cast because innerExecute mutates through StateView interface.
InnerExecuteInput StateTransitionContext::toInnerExecuteInput() const
{
    InnerExecuteInput input;
    input.state = const_cast<state::State*>(&state);
    input.vm = inputs.vm;
    input.message = message;
    input.gasPrice = gasPrice;
    input.blockInfo = inputs.blockInfo;
    input.blockHashes = inputs.blockHashes;
    input.revisionConfig = revisionConfig;
    input.accessList = inputs.accessList;
    input.authorizationListPresent = inputs.authorizationListPresent;
    input.authorizations = inputs.authorizations;
    input.web3TypedTxKind = inputs.web3TypedTxKind;
    input.extension = extension;
    input.callTargetPort = callTargetPort;
    return input;
}

}  // namespace bcos::evm
