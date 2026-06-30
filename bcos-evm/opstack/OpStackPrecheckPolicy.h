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
 * @file OpStackPrecheckPolicy.h
 */

#pragma once

#include "bcos-evm/eth/pipeline/ChainPrecheckPolicy.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"

namespace bcos::evm
{

struct OpStackPrecheckPolicy : ChainPrecheckPolicy
{
    explicit OpStackPrecheckPolicy(OpStackSettlementFacade& view);

    IntrinsicGasDebitParams intrinsicGasDebitParams() const override { return m_intrinsicPolicy; }

    /// Sync entry rules before buyGas (nonce, 7702, blob intent, fee caps). OpStack-only phase.
    void checkEntryRules(TxPipelineContext& ctx) const;

    void pipelineCheckGasAffordable(TxPipelineContext& ctx) const override;

    void pipelineTuneKernelInput(ExecuteMessageInput& input) const override;

    ExecuteMessageOutput pipelineInvokeEvmKernel(ExecuteMessageInput&& input) const override;

private:
    OpStackSettlementFacade& m_view;
    OpStackFeeSidecar& m_sidecar;
    IntrinsicGasDebitParams m_intrinsicPolicy{};
};

}  // namespace bcos::evm
