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
 * @file OpStackPrecheckPolicy.cpp
 */

#include "bcos-evm/opstack/OpStackPrecheckPolicy.h"
#include "bcos-evm/opstack/fee/OpStackFloorGasPrecheck.h"
#ifdef BCOS_EVM_TESTING
#include "bcos-evm/opstack/OpStackExecuteMessageTestHook.h"
#endif
#include <algorithm>

namespace bcos::evm
{

OpStackPrecheckPolicy::OpStackPrecheckPolicy(OpStackFeeContext& feeCtx) : m_feeCtx(feeCtx)
{
    m_intrinsicPolicy.mode = IntrinsicDebitMode::OpStackEntry;
    m_intrinsicPolicy.authTupleCount = feeCtx.m_authTupleCount;
    m_intrinsicPolicy.accessList = feeCtx.m_accessList;
    m_intrinsicPolicy.web3TypedTxKind = feeCtx.m_web3TypedTxKind;
}

void OpStackPrecheckPolicy::checkGasAffordable(TxPipelineContext& ctx) const
{
    auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
    bcos::bytesConstRef inputData{ctx.message.input_data, ctx.message.input_size};
    if (auto preDebitError = opStackFloorGasPrecheck({.message = ctx.message,
            .state = ctx.state,
            .gasLimit = gasLimit,
            .skipTransactionChecks = m_feeCtx.m_skipTransactionChecks,
            .inputData = inputData,
            .floorDataGasOut = m_feeCtx.m_floorDataGas});
        preDebitError.has_value())
    {
        ctx.evmcResult = std::move(*preDebitError);
        ctx.earlyExit = true;
    }
}

void OpStackPrecheckPolicy::tuneExecutionInput(ExecuteMessageInput& execInput) const
{
    if (m_feeCtx.m_isDepositTx)
    {
        execInput.skipTopLevelSenderNonceBump = true;
    }
}

ExecuteMessageOutput OpStackPrecheckPolicy::runEvmExecution(ExecuteMessageInput&& input) const
{
#ifdef BCOS_EVM_TESTING
    if (auto spyOutput = opstack::test::maybeCallExecuteMessageSpy(input); spyOutput.has_value())
    {
        return std::move(*spyOutput);
    }
#endif
    return executeMessage(std::move(input));
}

}  // namespace bcos::evm
