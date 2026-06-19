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
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace bcos::evm::state
{
EthHost::EthHost(State& state, evmc_tx_context txContext, evmc_revision revision, evmc::VM& vm,
    BlockHashes blockHashes, HostExtension* extension, bool fixStorageStatus)
  : m_state(state),
    m_txContext(txContext),
    m_revision(revision),
    m_vm(vm),
    m_blockHashes(std::move(blockHashes)),
    m_extension(extension),
    m_fixStorageStatus(fixStorageStatus)
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
    auto const slot = std::make_pair(addr, key);
    auto [it, inserted] = m_storageOriginalValues.try_emplace(slot, evmc_bytes32{});
    if (inserted)
    {
        // FISCO sstoreStatus reads committed storage directly for status classification.
        it->second = m_state.get_storage(addr, key);
    }

    m_state.set_storage(addr, key, value);
    return classifyStorageStatus(it->second, value, m_fixStorageStatus);
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
        if (auto precompiled = m_extension->tryChainPrecompile(m_revision, callMessage))
        {
            return Result(*precompiled);
        }
    }

    if (callMessage.kind == EVMC_DELEGATECALL && routed.hasPrecompileTarget &&
        m_extension != nullptr && !m_extension->allowDelegateCallToPrecompile())
    {
        return makeResult(EVMC_PRECOMPILE_FAILURE, callMessage.gas);
    }

    if (routed.hasPrecompileTarget)
    {
        if (auto precompiled =
                EthPrecompiles::tryDispatchInCall(routed.precompileTarget, callMessage, m_revision))
        {
            return std::move(*precompiled);
        }
    }

    if (m_extension != nullptr)
    {
        m_extension->prepareMessage(m_revision, callMessage);
    }

    if (!transferValue(callMessage))
    {
        return makeResult(EVMC_INSUFFICIENT_BALANCE, 0);
    }

    auto code = resolveExecutionCode(callMessage);
    m_state.checkpoint();
    auto result = m_vm.execute(*this, m_revision, callMessage, code.data(), code.size());
    if (result.status_code == EVMC_SUCCESS)
    {
        m_state.commit();
    }
    else
    {
        m_state.revert();
    }
    return result;
}

evmc_tx_context EthHost::get_tx_context() const noexcept
{
    return m_txContext;
}

EthHost::bytes32 EthHost::get_block_hash(int64_t number) const noexcept
{
    if (!m_blockHashes)
    {
        return {};
    }
    return m_blockHashes(number);
}

void EthHost::emit_log(const address& addr, const uint8_t* data, size_t data_size,
    const bytes32 topics[], size_t num_topics) noexcept
{
    LogEntry entry;
    entry.address = addr;
    if (data != nullptr && data_size > 0)
    {
        entry.data.assign(data, data + data_size);
    }
    entry.topics.reserve(num_topics);
    for (size_t i = 0; i < num_topics; ++i)
    {
        entry.topics.push_back(topics[i]);
    }
    m_logs.push_back(std::move(entry));
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

std::vector<LogEntry> EthHost::take_logs()
{
    return std::exchange(m_logs, {});
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
    const evmc_bytes32& oldValue, const evmc_bytes32& newValue, bool fixStorageStatus) noexcept
{
    auto const newZero = isZeroBytes32(newValue);
    if (!fixStorageStatus)
    {
        return newZero ? EVMC_STORAGE_DELETED : EVMC_STORAGE_MODIFIED;
    }

    auto const oldZero = isZeroBytes32(oldValue);
    if (newZero)
    {
        return oldZero ? EVMC_STORAGE_ASSIGNED : EVMC_STORAGE_DELETED;
    }
    if (oldZero)
    {
        return EVMC_STORAGE_ADDED;
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
            m_state.pin_warm_create_address(routed.message.code_address);
        }
        else if (!isZeroAddress(routed.message.code_address))
        {
            routed.message.recipient = routed.message.code_address;
            m_state.pin_warm_create_address(routed.message.code_address);
        }
    }

    auto target = isZeroAddress(routed.message.code_address) ? routed.message.recipient :
                                                               routed.message.code_address;
    if (!isZeroAddress(target))
    {
        routed.message.code_address = target;
        if (isZeroAddress(routed.message.recipient))
        {
            routed.message.recipient = target;
        }
    }
    auto const code = m_state.get_code(target);
    if (!isZeroAddress(target) && isBuiltinPrecompileAddress(target) && code.empty())
    {
        routed.precompileTarget = target;
        routed.hasPrecompileTarget = true;
        return routed;
    }

    return routed;
}

bcos::bytes EthHost::resolveExecutionCode(const evmc_message& msg) const
{
    if (isCreateKind(msg.kind))
    {
        if (msg.input_data == nullptr || msg.input_size == 0)
        {
            return {};
        }
        return bcos::bytes(msg.input_data, msg.input_data + msg.input_size);
    }
    auto const codeAddress = isZeroAddress(msg.code_address) ? msg.recipient : msg.code_address;
    return m_state.get_code(codeAddress);
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
