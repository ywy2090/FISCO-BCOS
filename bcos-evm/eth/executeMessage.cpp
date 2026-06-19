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
#include "bcos-evm/eth/execution/warmTransactionEntry.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
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

evmc_address resolveCodeAddress(const evmc_message& message) noexcept
{
    auto codeAddress = message.code_address;
    if (state::isZeroAddress(codeAddress))
    {
        codeAddress = message.recipient;
    }
    return codeAddress;
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

bool isBuiltinPrecompileAddress(const evmc_address& address) noexcept
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

evmc::Result makeSuccessResult(int64_t gasLeft)
{
    evmc_result result{};
    result.status_code = EVMC_SUCCESS;
    result.gas_left = gasLeft;
    return evmc::Result(result);
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

    auto const transaction = toStateTransaction(input.message);
    std::optional<evmc_address> createCodeAddress;
    if (isCreateKind(input.message.kind))
    {
        createCodeAddress = input.message.code_address;
    }
    execution::warmTransactionEntry(state, input.revisionConfig.revision, transaction,
        input.blockInfo, input.txProps, input.accessList, input.web3TypedTxKind, createCodeAddress);

    auto txContext = buildTxContext(input.blockInfo, input.message);
    txContext.tx_gas_price = state::toEvmC(input.gasPrice);
    state::EthHost host(state, txContext, input.revisionConfig.revision, *input.vm,
        input.blockHashes, input.extension, input.fixStorageStatus);

    bcos::bytes code;
    if (isCreateKind(input.message.kind))
    {
        code.assign(input.message.input_data, input.message.input_data + input.message.input_size);
    }
    else
    {
        auto const codeAddress = resolveCodeAddress(input.message);
        code = state.get_code(codeAddress);
        if (code.empty() && isBuiltinPrecompileAddress(codeAddress))
        {
            if (auto precompiled = state::EthPrecompiles::tryDispatchInCall(
                    codeAddress, input.message, input.revisionConfig.revision))
            {
                output.result = std::move(*precompiled);
                state.commit();
                output.stateDiff = state.build_diff();
                output.logs = host.take_logs();
                return output;
            }
        }
    }

    if (code.empty() && !isCreateKind(input.message.kind))
    {
        output.result = makeSuccessResult(input.message.gas);
        state.commit();
        output.stateDiff = state.build_diff();
        output.logs = host.take_logs();
        return output;
    }

    state.checkpoint();
    output.result = input.vm->execute(
        host, input.revisionConfig.revision, input.message, code.data(), code.size());
    output.logs = host.take_logs();

    if (output.result.status_code == EVMC_SUCCESS)
    {
        state.commit();
        output.stateDiff = state.build_diff();
    }
    else
    {
        state.revert();
    }

    return output;
}

}  // namespace bcos::evm
