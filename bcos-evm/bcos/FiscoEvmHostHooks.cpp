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
 * @file FiscoEvmHostHooks.cpp
 */

#include "bcos-evm/bcos/FiscoEvmHostHooks.h"
#include "bcos-evm/bcos/FiscoAddressDerivation.h"
#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/eip/Eip2929StorageGas.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-framework/ledger/Features.h"
#include <algorithm>
#include <cstring>

namespace bcos::evm
{

FiscoEvmHostHooks::FiscoEvmHostHooks(bool skipEvmNativeValueTransfer, FiscoEvmHostHooksDeps deps)
  : m_skipEvmNativeValueTransfer(skipEvmNativeValueTransfer)
{
    m_storageRef = deps.storageRef;
    m_blockHeader = deps.blockHeader;
    m_ledgerConfig = deps.ledgerConfig;
    m_blockNumber = deps.blockNumber;
    m_contextID = deps.contextID;
    m_seq = deps.seq;
    m_origin = deps.origin;
    m_revisionFlags = deps.revisionFlags;
    m_state = deps.state;
    m_hashImpl = deps.hashImpl;
    m_persistContractCreateNonce = std::move(deps.persistContractCreateNonce);
    m_authPort = deps.authPort;

    if (deps.recipientPathResolver)
    {
        m_recipientPathResolver = std::move(deps.recipientPathResolver);
    }
    else
    {
        m_recipientPathResolver = [](const evmc_message& message) {
            return std::string(USER_APPS_PREFIX) + hexAddress(message.recipient);
        };
    }
}

void FiscoEvmHostHooks::deriveNestedCreateAddress(evmc_message& message)
{
    if (shouldSkipNestedCreateDerivation(message, m_origin))
    {
        return;
    }

    bool const featureEvmAddress =
        m_ledgerConfig != nullptr &&
        m_ledgerConfig->features().get(ledger::Features::Flag::feature_evm_address);
    applyNestedCreateDerivation(message, FiscoNestedCreateParams{.web3Tx = m_revisionFlags.web3Tx,
                                             .featureEvmAddress = featureEvmAddress,
                                             .callerAddress = m_callerAddress,
                                             .origin = m_origin,
                                             .blockNumber = m_blockNumber,
                                             .contextID = m_contextID,
                                             .nestedSeq = m_seq,
                                             .state = m_state,
                                             .hashImpl = m_hashImpl});
}

void FiscoEvmHostHooks::prepareMessage(evmc_revision rev, evmc_message& msg)
{
    (void)rev;
    if (m_state == nullptr)
    {
        return;
    }

    deriveNestedCreateAddress(msg);

    if (m_blockNumber != 0 && m_authPort != nullptr)
    {
        const_cast<AuthPort*>(m_authPort)->createAuthTable(msg, resolveAuthTablePath(msg));
    }
    applyCreateNonceSemantics(msg);
}

bool FiscoEvmHostHooks::isZeroAddress(const evmc_address& address) noexcept
{
    return std::all_of(
        std::begin(address.bytes), std::end(address.bytes), [](uint8_t byte) { return byte == 0; });
}

evmc_address FiscoEvmHostHooks::createTarget(const evmc_message& message) noexcept
{
    return !isZeroAddress(message.code_address) ? message.code_address : message.recipient;
}

void FiscoEvmHostHooks::setCallerAddress(const evmc_address& caller)
{
    m_callerAddress = caller;
}

void FiscoEvmHostHooks::bumpContractCreateNonce(const evmc_address& contractAddress)
{
    if (m_state == nullptr || state::isZeroAddress(contractAddress) ||
        !m_persistContractCreateNonce)
    {
        return;
    }

    if (m_revisionFlags.web3Tx ||
        (m_ledgerConfig != nullptr &&
            m_ledgerConfig->features().get(ledger::Features::Flag::feature_evm_address)))
    {
        m_persistContractCreateNonce(contractAddress, m_state->get_nonce(contractAddress));
    }
}

void FiscoEvmHostHooks::applyCreateNonceSemantics(const evmc_message& message)
{
    if (m_state == nullptr)
    {
        return;
    }

    if (m_revisionFlags.fix_nonce_init)
    {
        m_state->set_nonce(createTarget(message), 1);
    }
}

void FiscoEvmHostHooks::applySstoreRefund(state::State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept
{
    if (m_revisionFlags.fix_storage_status)
    {
        state::applySstoreRefundEip3529(state, current, original, newValue);
    }
}

evmc_storage_status FiscoEvmHostHooks::classifyStorageStatus(evmc_bytes32 const& original,
    evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept
{
    if (m_revisionFlags.fix_storage_status)
    {
        return state::classifyStorageStatusPrecise(original, current, newValue);
    }
    return state::isZeroBytes32(newValue) ? EVMC_STORAGE_DELETED : EVMC_STORAGE_MODIFIED;
}

void FiscoEvmHostHooks::applyLegacySstoreDeletedRefund(
    state::State& state, evmc_storage_status status) const noexcept
{
    if (!m_revisionFlags.fix_storage_status && status == EVMC_STORAGE_DELETED)
    {
        state.add_refund(bcos::evm::gas::SSTORE_CLEARS_SCHEDULE_REFUND_EIP3529);
    }
}

void FiscoEvmHostHooks::finalizeTopLevelCreateNonce(
    state::State& state, evmc_address const& createAddr) noexcept
{
    if (m_revisionFlags.fix_nonce_init && !state::isZeroAddress(createAddr))
    {
        state.set_nonce(createAddr, 1);
    }
}

std::string FiscoEvmHostHooks::resolveAuthTablePath(const evmc_message& message) const
{
    if (m_revisionFlags.fix_auth_check && m_revisionFlags.use_raw_address)
    {
        return std::string(USER_APPS_PREFIX) + hexAddress(createTarget(message));
    }
    if (m_recipientPathResolver)
    {
        return m_recipientPathResolver(message);
    }
    return std::string(USER_APPS_PREFIX) + hexAddress(message.recipient);
}

std::string FiscoEvmHostHooks::hexAddress(const evmc_address& address)
{
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out(sizeof(address.bytes) * 2, '0');
    for (size_t i = 0; i < sizeof(address.bytes); ++i)
    {
        out[i * 2] = HEX[(address.bytes[i] >> 4U) & 0x0F];
        out[i * 2 + 1] = HEX[address.bytes[i] & 0x0F];
    }
    return out;
}

}  // namespace bcos::evm
