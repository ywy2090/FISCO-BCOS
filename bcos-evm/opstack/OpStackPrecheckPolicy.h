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
#include "bcos-evm/opstack/OpStackTxFeeLedger.h"

namespace bcos::evm
{

struct OpStackPrecheckPolicy : ChainPrecheckPolicy
{
    explicit OpStackPrecheckPolicy(OpStackFeeContext& feeCtx);

    IntrinsicGasPolicy intrinsicGasPolicy() const override { return m_intrinsicPolicy; }

    void checkGasAffordable(TxPipelineContext& ctx) const override;

    void tuneExecutionInput(ExecuteMessageInput& input) const override;

    ExecuteMessageOutput runEvmExecution(ExecuteMessageInput&& input) const override;

private:
    OpStackFeeContext& m_feeCtx;
    IntrinsicGasPolicy m_intrinsicPolicy{};
};

}  // namespace bcos::evm
