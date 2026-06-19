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
 * @file FiscoHostExtension.cpp
 */

#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace bcos::evm
{
namespace
{
constexpr std::string_view PRECOMPILED_CODE_FIELD = "[PRECOMPILED]";

bool startsWith(std::string_view input, std::string_view prefix) noexcept
{
    return input.size() >= prefix.size() &&
           std::memcmp(input.data(), prefix.data(), prefix.size()) == 0;
}

std::vector<std::string_view> splitByComma(std::string_view input)
{
    std::vector<std::string_view> tokens;
    size_t begin = 0;
    while (begin <= input.size())
    {
        auto const end = input.find(',', begin);
        if (end == std::string_view::npos)
        {
            tokens.push_back(input.substr(begin));
            break;
        }
        tokens.push_back(input.substr(begin, end - begin));
        begin = end + 1;
    }
    return tokens;
}
}  // namespace

FiscoHostExtension::FiscoHostExtension(
    bool skipEvmNativeValueTransfer, FiscoPrecompileCaller precompileCaller)
  : m_skipEvmNativeValueTransfer(skipEvmNativeValueTransfer),
    m_precompileCaller(std::move(precompileCaller))
{}

FiscoHostExtension::FiscoHostExtension(bool skipEvmNativeValueTransfer, FiscoHostExtensionDeps deps,
    FiscoPrecompileCaller precompileCaller)
  : FiscoHostExtension(skipEvmNativeValueTransfer, std::move(precompileCaller))
{
    m_storageRef = deps.storageRef;
    m_blockHeader = deps.blockHeader;
    m_ledgerConfig = deps.ledgerConfig;
    m_precompiledManager = deps.precompiledManager;
    m_blockNumber = deps.blockNumber;
    m_contextID = deps.contextID;
    m_seq = deps.seq;
    m_origin = deps.origin;
    m_revisionFlags = deps.revisionFlags;
    m_state = deps.state;
    m_externalCaller = std::move(deps.externalCaller);

    if (deps.recipientPathResolver)
    {
        m_recipientPathResolver = std::move(deps.recipientPathResolver);
    }
    else
    {
        m_recipientPathResolver = [](const evmc_message& message) {
            return std::string(executor::USER_APPS_PREFIX) + hexAddress(message.recipient);
        };
    }
    m_createAuthTableInvoker = std::move(deps.createAuthTableInvoker);
}

std::optional<evmc_result> FiscoHostExtension::tryChainPrecompile(
    evmc_revision rev, const evmc_message& msg)
{
    if (!m_precompileCaller)
    {
        return std::nullopt;
    }

    const evmc_address zero{};
    auto const target = std::memcmp(msg.code_address.bytes, zero.bytes, sizeof(zero.bytes)) != 0 ?
                            msg.code_address :
                            msg.recipient;
    auto resolvedTarget = target;
    if (m_state != nullptr)
    {
        auto const code = m_state->get_code(target);
        if (!code.empty())
        {
            std::string_view codeView(reinterpret_cast<const char*>(code.data()), code.size());
            if (auto dynamicTarget = parseDynamicPrecompileTarget(codeView))
            {
                resolvedTarget = *dynamicTarget;
            }
        }
    }

    if (!isFiscoPrecompileAddress(resolvedTarget))
    {
        return std::nullopt;
    }

    auto routedMessage = msg;
    routedMessage.recipient = resolvedTarget;
    routedMessage.code_address = resolvedTarget;
    return m_precompileCaller(rev, routedMessage);
}

void FiscoHostExtension::prepareMessage(evmc_revision rev, const evmc_message& msg)
{
    (void)rev;
    if (m_state == nullptr)
    {
        return;
    }

    if (m_blockNumber != 0 && m_createAuthTableInvoker)
    {
        m_createAuthTableInvoker(msg, resolveAuthTablePath(msg));
    }
    applyCreateNonceSemantics(msg);
}

bool FiscoHostExtension::isZeroAddress(const evmc_address& address) noexcept
{
    return std::all_of(
        std::begin(address.bytes), std::end(address.bytes), [](uint8_t byte) { return byte == 0; });
}

std::optional<evmc_address> FiscoHostExtension::parseDynamicPrecompileTarget(
    std::string_view code) noexcept
{
    if (!startsWith(code, PRECOMPILED_CODE_FIELD))
    {
        return std::nullopt;
    }

    auto const tokens = splitByComma(code);
    if (tokens.size() < 2)
    {
        return std::nullopt;
    }

    auto target = state::parseHexAddress(tokens[1]);
    if (state::isZeroAddress(target))
    {
        return std::nullopt;
    }
    return target;
}

evmc_address FiscoHostExtension::createTarget(const evmc_message& message) noexcept
{
    return !isZeroAddress(message.code_address) ? message.code_address : message.recipient;
}

void FiscoHostExtension::applyCreateNonceSemantics(const evmc_message& message)
{
    if (m_state == nullptr)
    {
        return;
    }
    if (m_revisionFlags.web3Tx && m_revisionFlags.createLevel != 0)
    {
        auto current = m_state->get_nonce(message.sender);
        m_state->set_nonce(message.sender, current + 1);
    }

    if (m_revisionFlags.fix_nonce_init)
    {
        m_state->set_nonce(createTarget(message), 1);
    }
}

std::string FiscoHostExtension::resolveAuthTablePath(const evmc_message& message) const
{
    if (m_revisionFlags.fix_auth_check && m_revisionFlags.use_raw_address)
    {
        return std::string(executor::USER_APPS_PREFIX) + hexAddress(createTarget(message));
    }
    if (m_recipientPathResolver)
    {
        return m_recipientPathResolver(message);
    }
    return std::string(executor::USER_APPS_PREFIX) + hexAddress(message.recipient);
}

std::string FiscoHostExtension::hexAddress(const evmc_address& address)
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

bool FiscoHostExtension::isFiscoPrecompileAddress(const evmc_address& address) noexcept
{
    constexpr uint64_t FISCO_PRECOMPILE_MIN = 0x1000;

    // Keep small-address semantics (same family as PrecompiledManager lookup).
    for (size_t i = 0; i < 12; ++i)
    {
        if (address.bytes[i] != 0)
        {
            return false;
        }
    }

    uint64_t value = 0;
    for (size_t i = 12; i < sizeof(address.bytes); ++i)
    {
        value = (value << 8U) | static_cast<uint64_t>(address.bytes[i]);
    }
    return value >= FISCO_PRECOMPILE_MIN;
}

}  // namespace bcos::evm
