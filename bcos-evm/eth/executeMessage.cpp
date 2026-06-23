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
 * @file executeMessage.cpp
 */

#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/Transfer.h"
#include "bcos-evm/eth/execution/warmTransactionEntry.h"
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"
#include "bcos-evm/eth/state/CreateExecution.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <optional>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

evmc_address resolveCreateAddress(const evmc_message& message, const evmc_result& result) noexcept
{
    auto createAddr = message.recipient;
    if (state::isZeroAddress(createAddr))
    {
        createAddr = message.code_address;
    }
    if (state::isZeroAddress(createAddr))
    {
        createAddr = result.create_address;
    }
    return createAddr;
}

evmc_address resolveCodeAddress(const evmc_message& message) noexcept
{
    auto codeAddress = message.code_address;
    if (state::isZeroAddress(codeAddress))
    {
        codeAddress = message.recipient;
    }
    return codeAddress;
}

bcos::bytes resolveExecutableCode(state::State& state, bcos::bytes code, bool eip7702Enabled)
{
    if (!eip7702Enabled || code.empty())
    {
        return code;
    }
    if (auto const delegate = parseDelegationTarget(bcos::bytesConstRef{code.data(), code.size()}))
    {
        return state.get_code(*delegate);
    }
    return code;
}

state::Transaction toStateTransaction(const evmc_message& message)
{
    state::Transaction transaction;
    transaction.from = message.sender;
    if (!isCreateKind(message.kind))
    {
        transaction.to = resolveCodeAddress(message);
    }
    transaction.data.assign(message.input_data, message.input_data + message.input_size);
    transaction.value = state::fromEvmC(message.value);
    transaction.gasLimit = message.gas;
    return transaction;
}

evmc_tx_context buildTxContext(const state::BlockInfo& block, const evmc_message& message)
{
    evmc_tx_context context{};
    context.tx_origin = message.sender;
    context.block_coinbase = block.coinbase;
    context.block_number = block.number;
    context.block_timestamp = block.timestamp;
    context.block_gas_limit = block.gasLimit;
    context.block_prev_randao = block.prevRandao;
    context.chain_id = state::toEvmC(block.chainId);
    context.block_base_fee = state::toEvmC(block.baseFee);
    context.blob_base_fee = state::toEvmC(block.blobBaseFee);
    return context;
}

evmc::Result makeInsufficientBalanceResult() noexcept
{
    evmc_result result{};
    result.status_code = EVMC_INSUFFICIENT_BALANCE;
    result.gas_left = 0;
    return evmc::Result(result);
}

bool applyTopLevelValueTransfer(state::State& state, ExecuteMessageInput const& input) noexcept
{
    if (input.message.depth != 0 || isCreateKind(input.message.kind))
    {
        return true;
    }
    if (input.extension != nullptr && input.extension->skipHostValueTransfer())
    {
        return true;
    }
    auto const value = state::fromEvmC(input.message.value);
    if (value == 0)
    {
        return true;
    }
    auto const recipient = resolveCodeAddress(input.message);
    if (!canTransfer(state, input.message.sender, value))
    {
        return false;
    }
    transfer(state, input.message.sender, recipient, value);
    return true;
}

state::State& resolveState(
    state::StateView const& stateView, std::optional<state::State>& stateCopy)
{
    if (auto* statePtr = dynamic_cast<state::State const*>(&stateView); statePtr != nullptr)
    {
        return *const_cast<state::State*>(statePtr);
    }

    stateCopy.emplace(stateView);
    return *stateCopy;
}
}  // namespace

ExecuteMessageOutput executeMessage(ExecuteMessageInput input)
{
    if (input.stateView == nullptr || input.vm == nullptr)
    {
        throw std::invalid_argument("executeMessage requires stateView/vm");
    }

    ExecuteMessageOutput output;
    std::optional<state::State> stateCopy;
    auto& state = resolveState(*input.stateView, stateCopy);
    state.clear_refund();

    auto const transaction = toStateTransaction(input.message);
    std::optional<evmc_address> createCodeAddress;
    if (isCreateKind(input.message.kind))
    {
        createCodeAddress = input.message.code_address;
    }
    execution::warmTransactionEntry(state, input.revisionConfig.revision, transaction,
        input.blockInfo, input.txProps, input.revisionConfig.warm_access, input.accessList,
        input.web3TypedTxKind, createCodeAddress);

    auto txContext = buildTxContext(input.blockInfo, input.message);
    txContext.tx_gas_price = state::toEvmC(input.gasPrice);
    state::EthHost host(state, txContext, input.revisionConfig, *input.vm, input.blockHashes,
        input.extension, input.fixStorageStatus);
    if (!isCreateKind(input.message.kind))
    {
        host.set_execution_address(resolveCodeAddress(input.message));
    }

    bcos::bytes code;
    if (isCreateKind(input.message.kind))
    {
        code.assign(input.message.input_data, input.message.input_data + input.message.input_size);
    }
    else
    {
        auto const codeAddress = resolveCodeAddress(input.message);
        if (input.revisionConfig.eip7702 && input.authorizationListPresent &&
            !input.authorizations.empty())
        {
            if (!state::isZeroAddress(input.message.sender))
            {
                auto const senderNonce = state.get_nonce(input.message.sender);
                state.set_nonce(input.message.sender, senderNonce + 1);
            }
            applyAuthorizations(state, input.authorizations, input.blockInfo.chainId);
            if (input.revisionConfig.warm_access && !state::isZeroAddress(codeAddress))
            {
                warmDelegationTarget(state, codeAddress);
            }
        }
        code = state.get_code(codeAddress);
        code = resolveExecutableCode(state, std::move(code), input.revisionConfig.eip7702);
        if (code.empty())
        {
            bool const skipVt = input.extension && input.extension->skipHostValueTransfer();
            auto routed = precompiled::dispatchPrecompile(
                {state, input.revisionConfig, input.extension, input.message, codeAddress, skipVt});
            if (routed.outcome != precompiled::PrecompileDispatchOutcome::NotApplicable)
            {
                output.result = std::move(routed.result);
                output.gasRefund = routed.gasRefund;
                output.stateDiff = state.build_diff();
                output.logs = host.take_logs();
                return output;
            }
        }
    }

    state.checkpoint();
    if (!applyTopLevelValueTransfer(state, input))
    {
        state.revert();
        output.result = makeInsufficientBalanceResult();
        output.logs = host.take_logs();
        return output;
    }
    if (isCreateKind(input.message.kind))
    {
        auto const initCode =
            bcos::bytesConstRef(input.message.input_data, input.message.input_size);
        state::bindCreateMessageForInit(host, input.message, initCode, state);
        auto const endowment = state::fromEvmC(input.message.value);
        if (endowment != 0)
        {
            if (!canTransfer(state, input.message.sender, endowment))
            {
                state.revert();
                output.result = makeInsufficientBalanceResult();
                output.logs = host.take_logs();
                return output;
            }
            transfer(state, input.message.sender, input.message.recipient, endowment);
        }
        state::initializeCreateTargetAccount(state, input.message.recipient,
            input.revisionConfig.revision, input.revisionConfig.warm_access);
    }
    output.result = input.vm->execute(
        host, input.revisionConfig.revision, input.message, code.data(), code.size());
    output.logs = host.take_logs();

    if (output.result.status_code == EVMC_SUCCESS && isCreateKind(input.message.kind))
    {
        auto raw = output.result.release_raw();
        if (!state::applyCreateCodeDepositGas(raw, input.revisionConfig.revision) &&
            raw.release != nullptr)
        {
            raw.release(&raw);
            raw.release = nullptr;
            raw.output_data = nullptr;
            raw.output_size = 0;
        }
        output.result = evmc::Result(raw);
    }
    if (output.result.status_code == EVMC_SUCCESS)
    {
        installCreatedContractCode(state, input.message, output.result.raw());
        if (isCreateKind(input.message.kind))
        {
            host.markCreatedInTx(resolveCreateAddress(input.message, output.result.raw()));
            auto& raw = const_cast<evmc_result&>(output.result.raw());
            if (state::isZeroAddress(raw.create_address))
            {
                raw.create_address = input.message.recipient;
            }
        }
        if (input.fixNonceInit && isCreateKind(input.message.kind))
        {
            auto createAddr = input.message.recipient;
            if (state::isZeroAddress(createAddr))
            {
                createAddr = input.message.code_address;
            }
            if (state::isZeroAddress(createAddr))
            {
                createAddr = output.result.create_address;
            }
            if (!state::isZeroAddress(createAddr))
            {
                state.set_nonce(createAddr, 1);
            }
        }
        output.gasRefund = static_cast<int64_t>(state.get_refund());
        state.commit();
        state.finalize_self_destructs();
        output.stateDiff = state.build_diff();
    }
    else
    {
        state.revert();
        output.gasRefund = static_cast<int64_t>(state.get_refund());
        output.stateDiff = state.build_diff();
    }

    return output;
}

}  // namespace bcos::evm
