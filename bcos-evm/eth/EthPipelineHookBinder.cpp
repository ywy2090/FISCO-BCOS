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
 * @file EthPipelineHookBinder.cpp
 */

#include "bcos-evm/eth/EthPipelineHookBinder.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/EthTxPrecheck.h"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/gas/Eip1559.h"
#include "bcos-evm/eth/orchestration/NormalizeIncludedTxVmerr.h"
#include "bcos-evm/eth/state/HashUtils.hpp"

namespace bcos::evm
{

TxPipelineHooks EthPipelineHookBinder::buildHooks(HookBindingContext& session)
{
    auto& input = session.input;
    auto& output = session.output;
    TxPipelineHooks hooks;

    hooks.preExecute = [&input](TxPipelineContext& orchestrationCtx) {
        if (auto preCheckError = ethTxPrecheck(input, orchestrationCtx.state))
        {
            orchestrationCtx.evmcResult = std::move(*preCheckError);
            orchestrationCtx.earlyExit = true;
            return;
        }

        auto const caps = gas::normalizeGasCaps(input.gasPrice, input.gasTipCap, input.gasFeeCap,
            input.web3TypedTxKind, input.hasExplicitFeeCaps);
        if (caps.isEip1559Caps)
        {
            orchestrationCtx.gasPrice = gas::resolveEffectiveGasPrice(
                caps.gasTipCap, caps.gasFeeCap, input.blockInfo.baseFee);
        }
    };

    hooks.intrinsicPolicy.mode = IntrinsicDebitMode::None;
    if (input.revisionConfig.eip7623)
    {
        hooks.intrinsicPolicy.mode = IntrinsicDebitMode::Eip7623;
    }
    else if (input.authorizationListPresent && !input.authorizations.empty())
    {
        hooks.intrinsicPolicy.mode = IntrinsicDebitMode::AuthOnly;
    }
    hooks.intrinsicPolicy.authorizationListPresent = input.authorizationListPresent;
    hooks.intrinsicPolicy.authTupleCount = input.authorizations.size();
    hooks.intrinsicPolicy.accessList = input.accessList;
    hooks.intrinsicPolicy.web3TypedTxKind = input.web3TypedTxKind;

    hooks.preKernel = [](TxPipelineContext& orchestrationCtx) {
        auto const txValue = state::fromEvmC(orchestrationCtx.message.value);
        if (txValue != 0 &&
            !canTransfer(orchestrationCtx.state, orchestrationCtx.message.sender, txValue))
        {
            evmc_result failResult{};
            failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
            failResult.gas_left = 0;
            orchestrationCtx.evmcResult =
                EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
            orchestrationCtx.earlyExit = true;
        }
    };

    hooks.postAdopt = [&output, &input](TxPipelineContext& orchestrationCtx) {
        normalizeSetCodeTransactionVmerr(
            orchestrationCtx.evmcResult, input.message.depth, input.authorizationListPresent);
        output.topLevelIncludedTxVmError = isTopLevelIncludedTxVmError(
            orchestrationCtx.evmcResult.status_code, input.message.depth);
        normalizeIncludedTxVmerr(orchestrationCtx.evmcResult, input.message.depth);
    };

    return hooks;
}

}  // namespace bcos::evm
