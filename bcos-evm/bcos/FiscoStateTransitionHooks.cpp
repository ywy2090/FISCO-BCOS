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
 * @file FiscoStateTransitionHooks.cpp
 */

#include "bcos-evm/bcos/FiscoStateTransitionHooks.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoPipelineInternals.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <boost/throw_exception.hpp>

namespace bcos::evm
{

FiscoStateTransitionHooks::FiscoStateTransitionHooks(
    FiscoExecutionRequest const& input, bool eip7623Enabled)
  : m_input(input), m_eip7623Enabled(eip7623Enabled)
{
    m_intrinsicPolicy.mode =
        eip7623Enabled ? IntrinsicDebitMode::Eip7623 : IntrinsicDebitMode::None;
    m_intrinsicPolicy.authorizationListPresent = input.authorizationListPresent;
    m_intrinsicPolicy.authTupleCount = input.authorizations.size();
    m_intrinsicPolicy.accessList = input.accessList.get();
    m_intrinsicPolicy.web3TypedTxKind = input.web3TypedTxKind;
}

void FiscoStateTransitionHooks::onNormalizeMessage(StateTransitionContext& ctx) const
{
    ctx.message = deriveMessage(FiscoTxAdapterInput{.web3Tx = m_input.web3Tx,
        .featureEvmAddress = m_input.revisionConfig.feature_evm_address,
        .message = ctx.message,
        .blockNumber = m_input.blockInfo.number,
        .contextID = m_input.contextID,
        .seq = m_input.seq,
        .nonce = m_input.nonce,
        .hashImpl = m_input.hashImpl});
}

void FiscoStateTransitionHooks::onPreCheckRules(StateTransitionContext& ctx) const
{
    if (m_input.revisionConfig.enable_auth_check && m_input.authPort != nullptr)
    {
        if (auto authResult = const_cast<AuthPort*>(m_input.authPort)->checkAuth(ctx.message);
            authResult.has_value())
        {
            ctx.evmcResult = std::move(*authResult);
            ctx.earlyExit = true;
            ctx.exitKind = StateTransitionExitKind::RulesRejected;
        }
    }
}

void FiscoStateTransitionHooks::onPreCheckCanTransfer(StateTransitionContext& ctx) const
{
    if (m_input.revisionConfig.enable_balance_transfer)
    {
        maybeTransferValue(
            ctx.state, ctx.message, m_input.revisionConfig.fix_delegatecall_transfer);
    }
    if (!m_eip7623Enabled)
    {
        if (ctx.message.gas < BALANCE_TRANSFER_GAS)
        {
            BOOST_THROW_EXCEPTION(protocol::OutOfGas{});
        }
        ctx.message.gas -= BALANCE_TRANSFER_GAS;
    }
    if (!isCreateKind(ctx.message.kind))
    {
        auto const code = ctx.state.get_code(ctx.message.code_address);
        if (code.empty() && ctx.message.input_size > 0)
        {
            BOOST_THROW_EXCEPTION(NotFoundCodeError{});
        }
    }
}

void FiscoStateTransitionHooks::onTuneInnerExecuteInput(InnerExecuteInput& executeInput) const
{
    executeInput.revisionConfig = m_input.revisionConfig.eth();
}

}  // namespace bcos::evm
