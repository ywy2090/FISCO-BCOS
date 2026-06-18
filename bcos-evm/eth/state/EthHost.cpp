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
 * @file EthHost.cpp
 */

#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace bcos::evm::state
{
namespace
{
constexpr std::string_view PRECOMPILED_CODE_FIELD = "[PRECOMPILED]";
constexpr size_t PRECOMPILED_CODE_FIELD_SIZE = 13;

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

EthHost::EthHost(
    State& state, evmc_tx_context txContext, evmc_revision revision, HostExtension* extension)
  : m_state(state), m_txContext(txContext), m_revision(revision), m_extension(extension)
{}

bool EthHost::account_exists(const address& addr) const noexcept
{
    return m_state.get_account(addr).has_value();
}

EthHost::bytes32 EthHost::get_storage(const address& addr, const bytes32& key) const noexcept
{
    return m_state.get_storage(addr, key);
}

evmc_storage_status EthHost::set_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    auto oldValue = m_state.get_storage(addr, key);
    m_state.set_storage(addr, key, value);
    return classifyStorageStatus(oldValue, value);
}

EthHost::uint256be EthHost::get_balance(const address& addr) const noexcept
{
    return toEvmC(m_state.get_balance(addr));
}

size_t EthHost::get_code_size(const address& addr) const noexcept
{
    return m_state.get_code(addr).size();
}

EthHost::bytes32 EthHost::get_code_hash(const address& addr) const noexcept
{
    return m_state.get_code_hash(addr);
}

size_t EthHost::copy_code(const address& addr, size_t code_offset, uint8_t* buffer_data,
    size_t buffer_size) const noexcept
{
    auto const code = m_state.get_code(addr);
    if (code_offset >= code.size() || buffer_size == 0)
    {
        return 0;
    }
    auto const count = std::min(buffer_size, code.size() - code_offset);
    std::copy_n(code.data() + code_offset, count, buffer_data);
    return count;
}

bool EthHost::selfdestruct(const address& addr, const address& beneficiary) noexcept
{
    (void)beneficiary;
    if (m_extension != nullptr)
    {
        auto const account = m_state.find(addr).value_or(Account{});
        if (!m_extension->allowSelfdestruct(account))
        {
            return false;
        }
    }
    return true;
}

EthHost::Result EthHost::call(const evmc_message& msg) noexcept
{
    auto routed = routeCall(msg);
    auto& callMessage = routed.message;

    if (m_extension != nullptr)
    {
        if (auto precompiled = m_extension->callFiscoPrecompile(m_revision, callMessage))
        {
            return Result(*precompiled);
        }
    }

    if (callMessage.kind == EVMC_DELEGATECALL && routed.hasPrecompileTarget &&
        m_extension != nullptr && !m_extension->allowDelegateCallToPrecompile())
    {
        return makeResult(EVMC_PRECOMPILE_FAILURE, callMessage.gas);
    }

    if (!transferValue(callMessage))
    {
        return makeResult(EVMC_INSUFFICIENT_BALANCE, 0);
    }

    return makeResult(EVMC_SUCCESS, callMessage.gas);
}

evmc_tx_context EthHost::get_tx_context() const noexcept
{
    return m_txContext;
}

EthHost::bytes32 EthHost::get_block_hash(int64_t number) const noexcept
{
    (void)number;
    return {};
}

void EthHost::emit_log(const address& addr, const uint8_t* data, size_t data_size,
    const bytes32 topics[], size_t num_topics) noexcept
{
    (void)addr;
    (void)data;
    (void)data_size;
    (void)topics;
    (void)num_topics;
}

evmc_access_status EthHost::access_account(const address& addr) noexcept
{
    return m_state.warm_up_address(addr) ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
}

evmc_access_status EthHost::access_storage(const address& addr, const bytes32& key) noexcept
{
    return m_state.warm_up_storage(addr, key) ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
}

EthHost::bytes32 EthHost::get_transient_storage(
    const address& addr, const bytes32& key) const noexcept
{
    auto const account = m_state.find(addr);
    if (!account.has_value())
    {
        return {};
    }
    auto it = account->transientStorage.find(key);
    if (it == account->transientStorage.end())
    {
        return {};
    }
    return it->second;
}

void EthHost::set_transient_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    m_state.set_transient_storage(addr, key, value);
}

bool EthHost::isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

bool EthHost::isBuiltinPrecompileAddress(const evmc_address& address) noexcept
{
    bool lowerBytesZero = true;
    for (size_t i = 0; i < 18; ++i)
    {
        if (address.bytes[i] != 0)
        {
            lowerBytesZero = false;
            break;
        }
    }
    if (!lowerBytesZero)
    {
        return false;
    }

    auto const high = address.bytes[18];
    auto const low = address.bytes[19];
    if (high == 0x00 && low >= 0x01 && low <= 0x11)
    {
        return true;
    }
    return high == 0x01 && low == 0x00;
}

std::optional<evmc_address> EthHost::parseDynamicPrecompileTarget(std::string_view code) noexcept
{
    if (code.size() <= PRECOMPILED_CODE_FIELD_SIZE || !startsWith(code, PRECOMPILED_CODE_FIELD))
    {
        return std::nullopt;
    }

    auto const tokens = splitByComma(code);
    if (tokens.size() < 2)
    {
        return std::nullopt;
    }

    auto target = parseHexAddress(tokens[1]);
    if (isZeroAddress(target))
    {
        return std::nullopt;
    }
    return target;
}

evmc::Result EthHost::makeResult(
    evmc_status_code status, int64_t gasLeft, const bcos::bytes& output)
{
    evmc_result result{};
    result.status_code = status;
    result.gas_left = gasLeft;
    result.gas_refund = 0;
    result.create_address = {};

    if (!output.empty())
    {
        auto* data = new uint8_t[output.size()];
        std::copy(output.begin(), output.end(), data);
        result.output_data = data;
        result.output_size = output.size();
        result.release = [](const evmc_result* value) { delete[] value->output_data; };
    }
    return evmc::Result(result);
}

evmc_storage_status EthHost::classifyStorageStatus(
    const evmc_bytes32& oldValue, const evmc_bytes32& newValue) noexcept
{
    auto const oldZero = isZeroBytes32(oldValue);
    auto const newZero = isZeroBytes32(newValue);
    if (oldZero && newZero)
    {
        return EVMC_STORAGE_ASSIGNED;
    }
    if (oldZero && !newZero)
    {
        return EVMC_STORAGE_ADDED;
    }
    if (!oldZero && newZero)
    {
        return EVMC_STORAGE_DELETED;
    }
    return EVMC_STORAGE_MODIFIED;
}

EthHost::RoutedCall EthHost::routeCall(const evmc_message& msg) noexcept
{
    RoutedCall routed{};
    routed.message = msg;

    if (isCreateKind(msg.kind))
    {
        if (!isZeroAddress(routed.message.recipient))
        {
            routed.message.code_address = routed.message.recipient;
            access_account(routed.message.code_address);
        }
        else if (!isZeroAddress(routed.message.code_address))
        {
            routed.message.recipient = routed.message.code_address;
            access_account(routed.message.code_address);
        }

        if (m_extension != nullptr)
        {
            m_extension->onCreateFrameEntry(m_revision, routed.message);
        }
    }

    auto target = routed.message.code_address;
    if (!isZeroAddress(target) && isBuiltinPrecompileAddress(target))
    {
        routed.precompileTarget = target;
        routed.hasPrecompileTarget = true;
        return routed;
    }

    auto const code = m_state.get_code(target);
    if (!code.empty())
    {
        std::string_view codeView(reinterpret_cast<const char*>(code.data()), code.size());
        if (auto dynamicTarget = parseDynamicPrecompileTarget(codeView))
        {
            routed.precompileTarget = *dynamicTarget;
            routed.hasPrecompileTarget = isBuiltinPrecompileAddress(*dynamicTarget);
            routed.message.recipient = *dynamicTarget;
        }
    }
    return routed;
}

bool EthHost::transferValue(const evmc_message& msg) noexcept
{
    if (isZeroBytes32(msg.value))
    {
        return true;
    }
    if (m_extension != nullptr && m_extension->skipHostValueTransfer())
    {
        return true;
    }

    auto const value = fromEvmC(msg.value);
    auto const from = m_state.get_balance(msg.sender);
    if (from < value)
    {
        return false;
    }
    auto const to = m_state.get_balance(msg.recipient);
    m_state.set_balance(msg.sender, from - value);
    m_state.set_balance(msg.recipient, to + value);
    return true;
}
}  // namespace bcos::evm::state
