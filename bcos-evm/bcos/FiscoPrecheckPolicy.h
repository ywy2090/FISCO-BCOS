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

    IntrinsicGasDebitParams intrinsicGasDebitParams() const override { return m_intrinsicPolicy; }

    void setupMessage(TxPipelineContext& ctx) const override;

    void checkTransactionRules(TxPipelineContext& ctx) const override;

    void checkBalanceAndValue(TxPipelineContext& ctx) const override;

    void tuneExecutionInput(ExecuteMessageInput& input) const override;

private:
    FiscoExecutionRequest const& m_input;
    bool m_eip7623Enabled{false};
    IntrinsicGasDebitParams m_intrinsicPolicy{};
};

}  // namespace bcos::evm
