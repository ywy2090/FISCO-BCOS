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
 * @file OpStackPipelineHookBinder.cpp
 */

#include "bcos-evm/opstack/OpStackPipelineHookBinder.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/opstack/OpStackFloorGasPrecheck.h"
#include "bcos-evm/opstack/OpStackGasSettlement.h"
#ifdef BCOS_EVM_TESTING
#include "bcos-evm/opstack/OpStackExecuteMessageTestHook.h"
#endif
#include <algorithm>

namespace bcos::evm
{
void OpStackPipelineHookBinder::applySettlement(
    HookBindingContext const& session, EVMCResult const& result)
{
    auto& txData = session.txData;
    // Use evmone's gas_refund directly — the State journal's refund counter
    // (ctx.state.get_refund()) can diverge from evmone's internal refund tracking
    // for complex contracts with many SSTORE operations.
    // EIP-3529 refund only applies London+ (eip1559 flag in RevisionConfig).
    auto const stateRefund = session.input.revisionConfig.eip1559 ?
                                 static_cast<uint64_t>(std::max<int64_t>(0, result.gas_refund)) :
                                 uint64_t{0};
    auto const settlement =
        postExecuteGasSettlement(static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit)),
            static_cast<uint64_t>(std::max<int64_t>(0, result.gas_left)), stateRefund,
            txData.m_floorDataGas);
    txData.m_gasRemaining = settlement.gasRemaining;
    txData.m_maxUsedGas = settlement.maxUsedGas;
    txData.m_gasUsed = static_cast<int64_t>(settlement.gasUsed);
}

OrchestrationHooks OpStackPipelineHookBinder::buildHooks(HookBindingContext& session)
{
    auto& txData = session.txData;
    OrchestrationHooks hooks;

    hooks.preDebitEntry = [&txData](OrchestrationContext& orchestrationCtx) {
        auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, txData.m_gasLimit));
        bcos::bytesConstRef inputData{
            orchestrationCtx.message.input_data, orchestrationCtx.message.input_size};
        if (auto preDebitError = opStackFloorGasPrecheck({.message = orchestrationCtx.message,
                .state = orchestrationCtx.state,
                .gasLimit = gasLimit,
                .skipTransactionChecks = txData.m_skipTransactionChecks,
                .inputData = inputData,
                .floorDataGasOut = txData.m_floorDataGas});
            preDebitError.has_value())
        {
            orchestrationCtx.evmcResult = std::move(*preDebitError);
            orchestrationCtx.earlyExit = true;
        }
    };

    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::OpStackEntry;
    hooks.intrinsicPolicy.authTupleCount = txData.m_authTupleCount;
    hooks.intrinsicPolicy.accessList = txData.m_accessList;
    hooks.intrinsicPolicy.web3TypedTxKind = txData.m_web3TypedTxKind;

    hooks.postSettle = [&session](OrchestrationContext& orchestrationCtx) {
        applySettlement(session, orchestrationCtx.evmcResult);
    };

#ifdef BCOS_EVM_TESTING
    hooks.executeMessageOverride = [](ExecuteMessageInput&& execInput) -> ExecuteMessageOutput {
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

}  // namespace bcos::evm
