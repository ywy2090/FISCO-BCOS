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
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackSettlementView.h"

namespace bcos::evm
{

struct OpStackPrecheckPolicy : ChainPrecheckPolicy
{
    explicit OpStackPrecheckPolicy(OpStackSettlementView& view);

    IntrinsicGasPolicy intrinsicGasPolicy() const override { return m_intrinsicPolicy; }

    /// Sync entry rules before buyGas (nonce, 7702, blob intent, fee caps). OpStack-only phase.
    void checkEntryRules(TxPipelineContext& ctx) const;

    void checkGasAffordable(TxPipelineContext& ctx) const override;

    void tuneExecutionInput(ExecuteMessageInput& input) const override;

    ExecuteMessageOutput runEvmExecution(ExecuteMessageInput&& input) const override;

private:
    OpStackSettlementView& m_view;
    OpStackFeeSidecar& m_sidecar;
    IntrinsicGasPolicy m_intrinsicPolicy{};
};

}  // namespace bcos::evm
