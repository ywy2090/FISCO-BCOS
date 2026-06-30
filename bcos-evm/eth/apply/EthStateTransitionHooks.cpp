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
 * @file EthStateTransitionHooks.cpp
 */

#include "bcos-evm/eth/apply/EthStateTransitionHooks.h"
#include "bcos-evm/eth/CanTransfer.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/apply/EthTxPrecheck.h"
#include "bcos-evm/eth/eip/Eip1559.h"
#include "bcos-evm/eth/eip/TxFeeSettlement.h"
#include "bcos-evm/eth/pipeline/FeeInputsMapping.h"
#include "bcos-evm/eth/state/HashUtils.hpp"

namespace bcos::evm
{

EthStateTransitionHooks::EthStateTransitionHooks(EthMessageRequest const& input) : m_input(input)
{
    m_intrinsicPolicy.mode = IntrinsicDebitMode::None;
    if (input.revisionConfig.eip7623)
    {
        m_intrinsicPolicy.mode = IntrinsicDebitMode::Eip7623;
    }
    else if (input.authorizationListPresent && !input.authorizations.empty())
    {
        m_intrinsicPolicy.mode = IntrinsicDebitMode::AuthOnly;
    }
    m_intrinsicPolicy.authorizationListPresent = input.authorizationListPresent;
    m_intrinsicPolicy.authTupleCount = input.authorizations.size();
    m_intrinsicPolicy.accessList = input.accessList;
    m_intrinsicPolicy.web3TypedTxKind = input.web3TypedTxKind;
}

void EthStateTransitionHooks::onPreCheckRules(StateTransitionContext& ctx) const
{
    if (auto preCheckError = ethTxPrecheck(m_input, ctx.state))
    {
        ctx.evmcResult = std::move(*preCheckError);
        ctx.earlyExit = true;
        return;
    }

    if (gas::isEip1559GasCapsTx(
            m_input.web3TypedTxKind, m_input.hasExplicitFeeCaps, m_input.revisionConfig))
    {
        auto const plan = gas::planPreExecution(gas::toFeeInputs(m_input, ctx.originalGasLimit));
        ctx.gasPrice = plan.effectiveGasPrice;
    }
}

void EthStateTransitionHooks::onPreCheckCanTransfer(StateTransitionContext& ctx) const
{
    auto const txValue = state::fromEvmC(ctx.message.value);
    if (txValue != 0 && !canTransfer(ctx.state, ctx.message.sender, txValue))
    {
        evmc_result failResult{};
        failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
        failResult.gas_left = 0;
        ctx.evmcResult = EVMCResult(failResult, protocol::TransactionStatus::InsufficientFunds);
        ctx.earlyExit = true;
    }
}

}  // namespace bcos::evm
