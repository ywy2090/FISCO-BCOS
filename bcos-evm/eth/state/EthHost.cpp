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
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
#include "bcos-evm/eth/state/CreateExecution.h"
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace bcos::evm::state
{
namespace
{
// geth core/vm/operations_acl.go gasSStoreEIP3529 / params.*
constexpr uint64_t kSstoreClearsScheduleRefundEip3529 = 4800;
constexpr uint64_t kSstoreSetGasEip2200 = 20000;
constexpr uint64_t kSstoreResetGasEip2200 = 5000;
constexpr uint64_t kColdSloadCostEip2929 = 2100;
constexpr uint64_t kWarmStorageReadCostEip2929 = 100;

void applySstoreRefundEip3529(State& state, const evmc_bytes32& current,
    const evmc_bytes32& original, const evmc_bytes32& value) noexcept
{
    if (Bytes32Equal{}(current, value))
    {
        return;
    }
    if (Bytes32Equal{}(original, current))
    {
        if (isZeroBytes32(original))
        {
            return;
        }
        if (isZeroBytes32(value))
        {
            state.add_refund(kSstoreClearsScheduleRefundEip3529);
        }
        return;
    }
    if (!isZeroBytes32(original))
    {
        if (isZeroBytes32(current))
        {
            state.sub_refund(kSstoreClearsScheduleRefundEip3529);
        }
        else if (isZeroBytes32(value))
        {
            state.add_refund(kSstoreClearsScheduleRefundEip3529);
        }
    }
    if (Bytes32Equal{}(original, value))
    {
        if (isZeroBytes32(original))
        {
            state.add_refund(kSstoreSetGasEip2200 - kWarmStorageReadCostEip2929);
        }
        else
        {
            state.add_refund(
                (kSstoreResetGasEip2200 - kColdSloadCostEip2929) - kWarmStorageReadCostEip2929);
        }
    }
}

// CALL/STATICCALL: recipient is the authority. DELEGATECALL/CALLCODE: recipient is the caller
// context and evmone puts the resolved delegate in code_address.
bool isDirectDelegated7702(evmc_message const& msg) noexcept
{
    return (msg.flags & EVMC_DELEGATED) != 0 && msg.kind == EVMC_CALL;
}
}  // namespace

EthHost::EthHost(State& state, evmc_tx_context txContext,
    bcos::evm_standard::RevisionConfig revisionConfig, evmc::VM& vm, BlockHashes blockHashes,
    HostExtension* extension, bool fixStorageStatus)
  : m_state(state),
    m_txContext(txContext),
    m_revisionConfig(revisionConfig),
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

    auto const& original = it->second;
    auto const current = m_state.get_storage(addr, key);
    if (Bytes32Equal{}(current, value))
    {
        // Unchanged slot: evmone keeps ASSIGNED (100 gas) even when original was zero.
        return EVMC_STORAGE_ASSIGNED;
    }
    if (m_fixStorageStatus)
    {
        applySstoreRefundEip3529(m_state, current, original, value);
    }

    m_state.set_storage(addr, key, value);
    auto const status = classifyStorageStatus(original, current, value, m_fixStorageStatus);
    if (!m_fixStorageStatus && status == EVMC_STORAGE_DELETED)
    {
        m_state.add_refund(kSstoreClearsScheduleRefundEip3529);
    }
    return status;
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

void EthHost::markCreatedInTx(evmc_address const& addr) noexcept
{
    if (!isZeroAddress(addr))
    {
        m_createdInTx.insert(addr);
    }
}

bool EthHost::wasCreatedInTx(evmc_address const& addr) const noexcept
{
    return m_createdInTx.contains(addr);
}

void EthHost::destroyContractState(evmc_address const& addr) noexcept
{
    m_state.set_code(addr, {}, {});
    m_state.clear_storage(addr);
}

bool EthHost::selfdestruct(const address& addr, const address& beneficiary) noexcept
{
    if (m_extension != nullptr)
    {
        auto const account = m_state.find(addr).value_or(Account{});
        if (!m_extension->allowSelfdestruct(account))
        {
            return false;
        }
    }

    auto const balance = m_state.get_balance(addr);
    auto const selfBeneficiary =
        std::memcmp(addr.bytes, beneficiary.bytes, sizeof(addr.bytes)) == 0;

    if (m_revisionConfig.revision >= EVMC_CANCUN && !wasCreatedInTx(addr))
    {
        // EIP-6780: pre-existing accounts are not destroyable; move balance only.
        // evmone: acc.balance = 0; beneficiary += balance (net unchanged when self-beneficiary).
        if (balance != 0)
        {
            m_state.set_balance(addr, 0);
            if (selfBeneficiary)
            {
                m_state.set_balance(addr, balance);
            }
            else
            {
                m_state.set_balance(beneficiary, m_state.get_balance(beneficiary) + balance);
            }
        }
        return false;
    }

    if (balance != 0)
    {
        if (!selfBeneficiary)
        {
            bcos::evm::transfer(m_state, addr, beneficiary, balance);
        }
        else
        {
            m_state.set_balance(addr, 0);
        }
    }

    if (m_revisionConfig.revision >= EVMC_CANCUN)
    {
        m_state.mark_self_destructed(addr);
        return true;
    }

    destroyContractState(addr);
    return true;
}

EthHost::Result EthHost::call(const evmc_message& msg) noexcept
{
    struct ExecutionAddressGuard
    {
        evmc_address& address;
        evmc_address saved;
        explicit ExecutionAddressGuard(evmc_address& address_) : address(address_), saved(address_)
        {}
        ~ExecutionAddressGuard() { address = saved; }
    };
    ExecutionAddressGuard guard{m_executionAddress};

    auto routed = routeCall(msg);
    auto& callMessage = routed.message;
    auto const callerAddress = resolveCallerAddress(callMessage);

    if (m_extension != nullptr)
    {
        if (auto precompiled =
                m_extension->tryChainPrecompile(m_revisionConfig.revision, callMessage))
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
        if (auto precompiled = EthPrecompiles::tryDispatchInCall(
                routed.precompileTarget, callMessage, m_revisionConfig.revision, m_revisionConfig))
        {
            return std::move(*precompiled);
        }
    }

    if (m_extension != nullptr)
    {
        m_extension->setCallerAddress(callerAddress);
        m_extension->prepareMessage(m_revisionConfig.revision, callMessage);
    }

    if (isCreateKind(callMessage.kind))
    {
        state::bindCreateMessageForInit(*this, callMessage,
            bcos::bytesConstRef(callMessage.input_data, callMessage.input_size), m_state);
    }

    m_state.checkpoint();
    if (isCreateKind(callMessage.kind))
    {
        state::initializeCreateTargetAccount(m_state, callMessage.recipient,
            m_revisionConfig.revision, m_revisionConfig.warm_access);
    }

    if (!transferValue(callMessage))
    {
        m_state.revert();
        return makeResult(EVMC_INSUFFICIENT_BALANCE, 0);
    }

    auto code = resolveExecutionCode(callMessage);
    auto result =
        m_vm.execute(*this, m_revisionConfig.revision, callMessage, code.data(), code.size());
    if (result.status_code == EVMC_SUCCESS && isCreateKind(callMessage.kind) &&
        !state::applyCreateCodeDepositGas(
            const_cast<evmc_result&>(result.raw()), m_revisionConfig.revision))
    {
        result = makeResult(result.status_code, result.gas_left);
    }
    if (result.status_code == EVMC_SUCCESS)
    {
        installCreatedContractCode(m_state, callMessage, result.raw());
        if (isCreateKind(callMessage.kind))
        {
            auto createAddr = callMessage.recipient;
            markCreatedInTx(createAddr);
            auto& raw = const_cast<evmc_result&>(result.raw());
            if (state::isZeroAddress(raw.create_address))
            {
                raw.create_address = createAddr;
            }
        }
        m_state.commit();
        if (!isCreateKind(callMessage.kind))
        {
            auto const nextExecution = isZeroAddress(callMessage.code_address) ?
                                           callMessage.recipient :
                                           callMessage.code_address;
            if (!isZeroAddress(nextExecution))
            {
                m_executionAddress = nextExecution;
            }
        }
    }
    else
    {
        m_state.revert();
    }
    if (isCreateKind(callMessage.kind) && !state::isZeroAddress(callMessage.sender))
    {
        m_state.set_nonce(callMessage.sender, m_state.get_nonce(callMessage.sender) + 1);
        if (m_extension != nullptr &&
            std::memcmp(callMessage.sender.bytes, m_txContext.tx_origin.bytes,
                sizeof(callMessage.sender.bytes)) != 0)
        {
            m_extension->bumpContractCreateNonce(callMessage.sender);
        }
    }
    return result;
}

evmc_address EthHost::resolveCallerAddress(const evmc_message& msg) const noexcept
{
    if (!state::isZeroAddress(m_executionAddress))
    {
        return m_executionAddress;
    }
    return msg.sender;
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
    if (!m_revisionConfig.warm_access)
    {
        return EVMC_ACCESS_COLD;
    }
    return m_state.warm_up_address(addr) ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
}

evmc_access_status EthHost::access_storage(const address& addr, const bytes32& key) noexcept
{
    if (!m_revisionConfig.warm_access)
    {
        return EVMC_ACCESS_COLD;
    }
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

bool EthHost::isActivePrecompileAddress(const evmc_address& address) const noexcept
{
    return precompiled::isActivePrecompile(m_revisionConfig.revision, m_revisionConfig, address);
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

evmc_storage_status EthHost::classifyStorageStatus(const evmc_bytes32& oldValue,
    const evmc_bytes32& currentValue, const evmc_bytes32& newValue, bool fixStorageStatus) noexcept
{
    if (!fixStorageStatus)
    {
        return isZeroBytes32(newValue) ? EVMC_STORAGE_DELETED : EVMC_STORAGE_MODIFIED;
    }

    // evmone test/state/host.cpp set_storage (EIP-2200 / EIP-3529 status mapping).
    auto const dirty = !Bytes32Equal{}(oldValue, currentValue);
    auto const restored = Bytes32Equal{}(oldValue, newValue);
    auto const currentIsZero = isZeroBytes32(currentValue);
    auto const valueIsZero = isZeroBytes32(newValue);

    auto status = EVMC_STORAGE_ASSIGNED;
    if (!dirty && !restored)
    {
        if (currentIsZero)
        {
            status = EVMC_STORAGE_ADDED;
        }
        else if (valueIsZero)
        {
            status = EVMC_STORAGE_DELETED;
        }
        else
        {
            status = EVMC_STORAGE_MODIFIED;
        }
    }
    else if (dirty && !restored)
    {
        if (currentIsZero && !valueIsZero)
        {
            status = EVMC_STORAGE_DELETED_ADDED;
        }
        else if (!currentIsZero && valueIsZero)
        {
            status = EVMC_STORAGE_MODIFIED_DELETED;
        }
    }
    else if (dirty && restored)
    {
        if (currentIsZero)
        {
            status = EVMC_STORAGE_DELETED_RESTORED;
        }
        else if (valueIsZero)
        {
            status = EVMC_STORAGE_ADDED_DELETED;
        }
        else
        {
            status = EVMC_STORAGE_MODIFIED_RESTORED;
        }
    }
    return status;
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
            if (m_revisionConfig.warm_access)
            {
                m_state.pin_warm_create_address(routed.message.code_address);
            }
        }
        else if (!isZeroAddress(routed.message.code_address))
        {
            routed.message.recipient = routed.message.code_address;
            if (m_revisionConfig.warm_access)
            {
                m_state.pin_warm_create_address(routed.message.code_address);
            }
        }
    }

    auto target = isZeroAddress(routed.message.code_address) ? routed.message.recipient :
                                                               routed.message.code_address;
    // EIP-7702: CALL/STATICCALL pass the authority as recipient; DELEGATECALL/CALLCODE keep the
    // resolved delegate in code_address. Delegation to a precompile runs empty code.
    if (isDirectDelegated7702(msg))
    {
        target = routed.message.recipient;
    }
    if (!isZeroAddress(target))
    {
        routed.message.code_address = target;
        if (isZeroAddress(routed.message.recipient))
        {
            routed.message.recipient = target;
        }
    }
    auto const code = m_state.get_code(target);
    if ((msg.flags & EVMC_DELEGATED) == 0 && !isZeroAddress(target) &&
        isActivePrecompileAddress(target) && code.empty())
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
    if ((msg.flags & EVMC_DELEGATED) != 0)
    {
        if (msg.kind == EVMC_DELEGATECALL || msg.kind == EVMC_CALLCODE)
        {
            if (isActivePrecompileAddress(msg.code_address))
            {
                return {};
            }
            auto code = m_state.get_code(msg.code_address);
            if (m_revisionConfig.eip7702)
            {
                if (auto const delegate =
                        parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
                {
                    return m_state.get_code(*delegate);
                }
            }
            return code;
        }
    }
    auto const codeAddress =
        isDirectDelegated7702(msg) ?
            msg.recipient :
            (isZeroAddress(msg.code_address) ? msg.recipient : msg.code_address);
    auto code = m_state.get_code(codeAddress);
    if (m_revisionConfig.eip7702)
    {
        if (auto const delegate =
                parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
        {
            return m_state.get_code(*delegate);
        }
    }
    return code;
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
    if (!canTransfer(m_state, msg.sender, value))
    {
        return false;
    }
    transfer(m_state, msg.sender, msg.recipient, value);
    return true;
}
}  // namespace bcos::evm::state
