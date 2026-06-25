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
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/opstack/OpStackBlobTxIntent.h"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/fee/OpStackFloorGasPrecheck.h"
#ifdef BCOS_EVM_TESTING
#include "bcos-evm/opstack/OpStackExecuteMessageTestHook.h"
#endif
#include <algorithm>

namespace bcos::evm
{
namespace
{
inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

EVMCResult makePreCheckError(
    protocol::TransactionStatus status, evmc_status_code evmcStatus = EVMC_FAILURE)
{
    evmc_result result{};
    result.status_code = evmcStatus;
    result.gas_left = 0;
    return EVMCResult(result, status);
}
}  // namespace

OpStackPrecheckPolicy::OpStackPrecheckPolicy(
    OpStackExecutionRequest const& input, OpStackFeeContext& feeCtx)
  : m_input(input), m_feeCtx(feeCtx)
{
    m_intrinsicPolicy.mode = IntrinsicDebitMode::OpStackEntry;
    m_intrinsicPolicy.authTupleCount = feeCtx.m_authTupleCount;
    m_intrinsicPolicy.accessList = feeCtx.m_accessList;
    m_intrinsicPolicy.web3TypedTxKind = feeCtx.m_web3TypedTxKind;
}

void OpStackPrecheckPolicy::checkEntryRules(TxPipelineContext& ctx) const
{
    auto const deposit = isDepositTx(m_input);

    if (deposit)
    {
        if (m_input.depositTx.has_value() && m_input.depositTx->isSystemTransaction)
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
        }
        return;
    }

    if (!m_input.skipNonceChecks)
    {
        auto const stateNonce = ctx.state.get_nonce(m_input.message.sender);
        if (stateNonce != m_input.nonce)
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::NonceCheckFail);
            ctx.earlyExit = true;
            return;
        }
    }

    if (!m_input.skipTransactionChecks)
    {
        auto const senderCode = ctx.state.get_code(m_input.message.sender);
        if (!senderCode.empty() &&
            !parseDelegationTarget(bcos::bytesConstRef{senderCode.data(), senderCode.size()})
                 .has_value())
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }

        if (m_input.gasFeeCap < m_input.gasTipCap || m_input.gasFeeCap < m_input.blockInfo.baseFee)
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }

        if (hasBlobTxIntent(m_input))
        {
            if (!m_input.revisionConfig.eip4844)
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
            if (isCreateKind(m_input.message.kind))
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
            if (m_input.blobVersionedHashes.empty())
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
            for (auto const& hash : m_input.blobVersionedHashes)
            {
                if (!isValidVersionedHash(hash))
                {
                    ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                    ctx.earlyExit = true;
                    return;
                }
            }
            if (m_input.blobGasFeeCap < m_input.blockInfo.blobBaseFee)
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::InsufficientFunds);
                ctx.earlyExit = true;
                return;
            }
        }

        if (!m_input.authorizations.empty() && isCreateKind(m_input.message.kind))
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }
        if (m_input.authorizationListPresent && m_input.authorizations.empty())
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
        }
    }
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
