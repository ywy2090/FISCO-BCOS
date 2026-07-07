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
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/apply/EthEvmResult.h"
#include "bcos-evm/eth/eip/Eip1559.h"
#include "bcos-evm/eth/eip/Eip1559Gate.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/eth/eip/Eip3860.h"
#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/eip/Eip7825.h"
#include "bcos-evm/eth/gas/TxFeeSettlement.h"
#include "bcos-evm/eth/kernel/state-transition/FeeInputsMapping.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include "eth/RevisionConfig.h"
#include "eth/gas/GasSettlementTypes.h"
#include "eth/kernel/EVMCResult.h"
#include "eth/state/BlockInfo.hpp"
#include "eth/state/State.hpp"
#include <evmc/evmc.h>
#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <optional>
#include <vector>

namespace bcos::evm
{

EthStateTransitionHooks::EthStateTransitionHooks(EthMessageRequest const& input) : m_input(input)
{
    // Pre-7623 Eth reference txs debit full intrinsic (base + calldata + access list) before EVM.
    m_intrinsicPolicy.mode = IntrinsicGasMode::OpStack;
    if (input.revisionConfig.eip7623)
    {
        m_intrinsicPolicy.mode = IntrinsicGasMode::FloorDataGas;
    }
    else if (input.authorizationListPresent && !input.authorizations.empty())
    {
        m_intrinsicPolicy.mode = IntrinsicGasMode::AuthTuples;
    }
    m_intrinsicPolicy.authorizationListPresent = input.authorizationListPresent;
    m_intrinsicPolicy.authTupleCount = input.authorizations.size();
    m_intrinsicPolicy.accessList = input.accessList;
    m_intrinsicPolicy.web3TypedTxKind = input.web3TypedTxKind;
}

void EthStateTransitionHooks::onPreCheckRules(StateTransitionContext& ctx) const
{
    if (isTxGasLimitExceeded(m_input.revisionConfig, ctx.originalGasLimit))
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::OutOfGasLimit);
        ctx.earlyExit = true;
        return;
    }

    if (m_input.blockInfo.gasLimit > 0 && ctx.originalGasLimit > m_input.blockInfo.gasLimit)
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::OutOfGasLimit);
        ctx.earlyExit = true;
        return;
    }

    // EIP-2681: account nonce cannot exceed uint64 max; reject txs that cannot be incremented.
    if (m_input.txNonce == std::numeric_limits<uint64_t>::max())
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::NonceCheckFail);
        ctx.earlyExit = true;
        return;
    }

    auto const senderCode = ctx.state.get_code(m_input.message.sender);
    if (!senderCode.empty() &&
        !parseDelegationTarget(bcos::bytesConstRef{senderCode.data(), senderCode.size()})
             .has_value())
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
        ctx.earlyExit = true;
        return;
    }

    if (gas::isEip1559FeeMarketActive(m_input.revisionConfig))
    {
        if (m_input.gasFeeCap < m_input.gasTipCap || m_input.gasFeeCap < m_input.blockInfo.baseFee)
        {
            ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }
    }

    if (m_input.authorizationListPresent && m_input.authorizations.empty())
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
        ctx.earlyExit = true;
        return;
    }

    if (!m_input.authorizations.empty() &&
        (m_input.message.kind == EVMC_CREATE || m_input.message.kind == EVMC_CREATE2))
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
        ctx.earlyExit = true;
        return;
    }

    if (m_input.web3TypedTxKind == toWeb3TypedTxKindValue(Web3TypedTxKind::EIP7702) &&
        (m_input.message.kind == EVMC_CREATE || m_input.message.kind == EVMC_CREATE2))
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
        ctx.earlyExit = true;
        return;
    }

    if (!isTypedTxKindSupportedByRevision(m_input.web3TypedTxKind, m_input.revisionConfig))
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
        ctx.earlyExit = true;
        return;
    }

    if (isInitCodeSizeExceeded(m_input.revisionConfig.revision, m_input.message.kind,
            static_cast<size_t>(m_input.message.input_size)))
    {
        ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
        ctx.earlyExit = true;
        return;
    }

    if (gas::hasBlobTxIntent(m_input.web3TypedTxKind, !m_input.blobVersionedHashes.empty(),
            m_input.blobGasFeeCap > 0))
    {
        if (!m_input.revisionConfig.eip4844)
        {
            ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }
        if (m_input.message.kind == EVMC_CREATE || m_input.message.kind == EVMC_CREATE2)
        {
            ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }
        if (m_input.blobVersionedHashes.empty())
        {
            ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }
        if (m_input.blobVersionedHashes.size() > gas::MAX_BLOBS_PER_TX)
        {
            ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
            ctx.earlyExit = true;
            return;
        }
        for (auto const& hash : m_input.blobVersionedHashes)
        {
            if (hash[0] != 0x01)
            {
                ctx.evmcResult = makeEvmcResult(protocol::TransactionStatus::Malformed);
                ctx.earlyExit = true;
                return;
            }
        }
        if (m_input.blobGasFeeCap < m_input.blockInfo.blobBaseFee)
        {
            ctx.evmcResult = makeEvmcResult(
                protocol::TransactionStatus::InsufficientFunds, EVMC_INSUFFICIENT_BALANCE);
            ctx.earlyExit = true;
            return;
        }
    }

    if (gas::isEip1559GasCapsTx(
            m_input.web3TypedTxKind, m_input.hasExplicitFeeCaps, m_input.revisionConfig))
    {
        auto const plan =
            gas::planPreExecution(gas::toFeeInputs(m_input.revisionConfig, m_input.blockInfo,
                gas::FeeCapsView{m_input.gasPrice, m_input.gasTipCap, m_input.gasFeeCap,
                    m_input.web3TypedTxKind, m_input.hasExplicitFeeCaps},
                ctx.originalGasLimit));
        ctx.gasPrice = plan.effectiveGasPrice;
    }
}

void EthStateTransitionHooks::onPreCheckCanTransfer(StateTransitionContext& ctx) const
{
    auto const txValue = state::fromEvmC(ctx.message.value);
    if (txValue != 0 && ctx.state.get_balance(ctx.message.sender) < txValue)
    {
        ctx.evmcResult = makeEvmcResult(
            protocol::TransactionStatus::InsufficientFunds, EVMC_INSUFFICIENT_BALANCE);
        ctx.earlyExit = true;
    }
}

}  // namespace bcos::evm
