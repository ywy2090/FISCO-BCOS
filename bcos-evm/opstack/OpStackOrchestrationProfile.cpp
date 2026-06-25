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
 * @file OpStackOrchestrationProfile.cpp
 */

#include "bcos-evm/opstack/OpStackOrchestrationProfile.h"
#include "bcos-evm/opstack/fee/OpStackFloorGasPrecheck.h"
#ifdef BCOS_EVM_TESTING
#include "bcos-evm/opstack/OpStackExecuteMessageTestHook.h"
#endif
#include <algorithm>

namespace bcos::evm
{

TxPipelineHooks OpStackOrchestrationProfile::buildHooks(Session& session)
{
    auto& feeCtx = session.feeCtx;
    TxPipelineHooks hooks;

    hooks.txCheckGasAffordable = [&feeCtx](TxPipelineContext& orchestrationCtx) {
        auto const gasLimit =
            static_cast<uint64_t>(std::max<int64_t>(0, orchestrationCtx.originalGasLimit));
        bcos::bytesConstRef inputData{
            orchestrationCtx.message.input_data, orchestrationCtx.message.input_size};
        if (auto preDebitError = opStackFloorGasPrecheck({.message = orchestrationCtx.message,
                .state = orchestrationCtx.state,
                .gasLimit = gasLimit,
                .skipTransactionChecks = feeCtx.m_skipTransactionChecks,
                .inputData = inputData,
                .floorDataGasOut = feeCtx.m_floorDataGas});
            preDebitError.has_value())
        {
            orchestrationCtx.evmcResult = std::move(*preDebitError);
            orchestrationCtx.earlyExit = true;
        }
    };

    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::OpStackEntry;
    hooks.intrinsicPolicy.authTupleCount = feeCtx.m_authTupleCount;
    hooks.intrinsicPolicy.accessList = feeCtx.m_accessList;
    hooks.intrinsicPolicy.web3TypedTxKind = feeCtx.m_web3TypedTxKind;

    if (feeCtx.m_isDepositTx)
    {
        hooks.txTuneExecutionInput = [](ExecuteMessageInput& execInput) {
            execInput.skipTopLevelSenderNonceBump = true;
        };
    }

#ifdef BCOS_EVM_TESTING
    hooks.txRunEvmExecutionOverride = [](ExecuteMessageInput&& execInput) -> ExecuteMessageOutput {
        if (auto spyOutput = opstack::test::maybeCallExecuteMessageSpy(execInput);
            spyOutput.has_value())
        {
            return std::move(*spyOutput);
        }
        return executeMessage(std::move(execInput));
    };
#endif

    return hooks;
}

OpStackOrchestrationErrorPolicy OpStackOrchestrationProfile::buildErrorPolicy(
    Session const& /*session*/)
{
    return OpStackOrchestrationErrorPolicy{};
}

OpStackOrchestrationProfile::Bindings OpStackOrchestrationProfile::bind(Session& session)
{
    return Bindings{buildHooks(session), buildErrorPolicy(session)};
}

}  // namespace bcos::evm
