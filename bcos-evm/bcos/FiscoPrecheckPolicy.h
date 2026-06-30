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
 * @file FiscoPrecheckPolicy.h
 */

#pragma once

#include "bcos-evm/bcos/FiscoExecute.h"
#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"

namespace bcos::evm
{

struct FiscoPrecheckPolicy : ChainPrecheckPolicy
{
    FiscoPrecheckPolicy(FiscoExecutionRequest const& input, bool eip7623Enabled);

    DeductIntrinsicGasParams deductIntrinsicGasParams() const override { return m_intrinsicPolicy; }

    void pipelineSetupMessage(StateTransitionContext& ctx) const override;

    void pipelineCheckRules(StateTransitionContext& ctx) const override;

    void pipelineCheckBalance(StateTransitionContext& ctx) const override;

    void pipelineTuneKernelInput(InnerExecuteInput& input) const override;

private:
    FiscoExecutionRequest const& m_input;
    bool m_eip7623Enabled{false};
    DeductIntrinsicGasParams m_intrinsicPolicy{};
};

}  // namespace bcos::evm
