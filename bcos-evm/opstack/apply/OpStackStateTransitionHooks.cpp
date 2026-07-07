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
 * @file OpStackStateTransitionHooks.cpp
 */

#include "bcos-evm/opstack/apply/OpStackStateTransitionHooks.h"
#include "bcos-evm/eth/eip/Eip1559Gate.h"
#include "bcos-evm/eth/eip/Eip3860.h"
#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/eip/Eip7825.h"
#include "bcos-evm/eth/kernel/execution/InnerExecute.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/opstack/fee/OpStackFloorGasPrecheck.h"
#include "bcos-evm/opstack/types/OpStackBlobTxChecks.h"
#include "bcos-evm/opstack/types/OpStackDepositTx.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/Common.h"
#include "eth/RevisionConfig.h"
#include "eth/kernel/EVMCResult.h"
#include "eth/state/BlockInfo.hpp"
#include "eth/state/State.hpp"
#include "opstack/apply/ApplyOpStackMessage.h"
#include "opstack/settlement/OpStackFeeSidecar.h"
#include "opstack/settlement/OpStackSettlementProjection.h"
#include <evmc/evmc.h>
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace bcos::evm
{
namespace
{
EVMCResult makePreCheckError(
    protocol::TransactionStatus status, evmc_status_code evmcStatus = EVMC_FAILURE)
{
    evmc_result result{};
    result.status_code = evmcStatus;
    result.gas_left = 0;
    return EVMCResult(result, status);
}
}  // namespace

OpStackStateTransitionHooks::OpStackStateTransitionHooks(OpStackSettlementProjection& view)
  : m_view(view), m_sidecar(view.mutableSidecar())
{
    m_intrinsicPolicy.mode = IntrinsicGasMode::OpStack;
    m_intrinsicPolicy.authTupleCount = view.authTupleCount();
    m_intrinsicPolicy.accessList = view.accessList();
    m_intrinsicPolicy.web3TypedTxKind = view.web3TypedTxKind();
    m_intrinsicPolicy.eip3860 = view.input.revisionConfig.eip3860;
}

void OpStackStateTransitionHooks::lifecycleCheckEntryRules(StateTransitionContext& ctx) const
{
    auto const& input = m_view.input;
    auto const deposit = m_view.isDeposit();

    if (deposit)
    {
        if (input.depositTx.has_value() && input.depositTx->isSystemTransaction)
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
        }
        return;
    }

    if (!input.skipNonceChecks)
    {
        auto const stateNonce = ctx.state.get_nonce(input.message.sender);
        if (stateNonce != input.nonce)
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::NonceCheckFail);
            ctx.earlyExit = true;
            return;
        }
    }

    if (!input.skipTransactionChecks)
    {
        if (isTxGasLimitExceeded(input.revisionConfig, ctx.originalGasLimit))
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::OutOfGasLimit);
            ctx.earlyExit = true;
            return;
        }

        auto const senderCode = ctx.state.get_code(input.message.sender);
        if (!senderCode.empty() &&
            !parseDelegationTarget(bcos::bytesConstRef{senderCode.data(), senderCode.size()})
                 .has_value())
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }

        if (gas::isEip1559FeeMarketActive(input.revisionConfig))
        {
            if (input.gasFeeCap < input.gasTipCap || input.gasFeeCap < input.blockInfo.baseFee)
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
        }

        if (hasBlobTxIntent(input))
        {
            if (!input.revisionConfig.eip4844)
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
            if (input.message.kind == EVMC_CREATE || input.message.kind == EVMC_CREATE2)
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
            if (input.blobVersionedHashes.empty())
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
            if (input.blobVersionedHashes.size() >
                gas::maxBlobsPerTx(input.revisionConfig.revision))
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
            for (auto const& hash : input.blobVersionedHashes)
            {
                if (!isValidVersionedHash(hash))
                {
                    ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
                    ctx.earlyExit = true;
                    return;
                }
            }
            if (input.blobGasFeeCap < input.blockInfo.blobBaseFee)
            {
                ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::InsufficientFunds);
                ctx.earlyExit = true;
                return;
            }
        }

        if (!input.authorizations.empty() &&
            (input.message.kind == EVMC_CREATE || input.message.kind == EVMC_CREATE2))
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }
        if (input.authorizationListPresent && input.authorizations.empty())
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }

        if (isInitCodeSizeExceeded(input.revisionConfig.eip3860, input.message.kind,
                static_cast<size_t>(input.message.input_size)))
        {
            ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
        }
    }
}

void OpStackStateTransitionHooks::onPreCheckGasAffordable(StateTransitionContext& ctx) const
{
    auto const gasLimit = static_cast<uint64_t>(std::max<int64_t>(0, ctx.originalGasLimit));
    bcos::bytesConstRef inputData{ctx.message.input_data, ctx.message.input_size};
    if (auto preDebitError = opStackFloorGasPrecheck({.message = ctx.message,
            .state = ctx.state,
            .gasLimit = gasLimit,
            .skipTransactionChecks = m_view.skipTransactionChecks(),
            .inputData = inputData,
            .floorDataGasOut = m_sidecar.floorDataGas});
        preDebitError.has_value())
    {
        ctx.evmcResult = std::move(*preDebitError);
        ctx.earlyExit = true;
    }
}

void OpStackStateTransitionHooks::onTuneInnerExecuteInput(InnerExecuteInput& execInput) const
{
    if (m_view.isDeposit())
    {
        execInput.skipTopLevelSenderNonceBump = true;
    }
}

InnerExecuteOutput OpStackStateTransitionHooks::onInvokeInnerExecute(
    InnerExecuteInput&& input) const
{
    return innerExecute(std::move(input));
}

}  // namespace bcos::evm
