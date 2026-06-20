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
#include "bcos-evm/eth/precompiled/PrecompileActive.h"
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
    else
    {
        auto createAddr = input.message.recipient;
        if (state::isZeroAddress(createAddr))
        {
            createAddr = input.message.code_address;
        }
        if (!state::isZeroAddress(createAddr))
        {
            host.set_execution_address(createAddr);
        }
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
            applyAuthorizations(state, input.authorizations, input.blockInfo.chainId);
            if (input.revisionConfig.warm_access && !state::isZeroAddress(codeAddress))
            {
                warmDelegationTarget(state, codeAddress);
            }
        }
        code = state.get_code(codeAddress);
        if (code.empty() && precompiled::isActivePrecompile(
                                input.revisionConfig.revision, input.revisionConfig, codeAddress))
        {
            if (auto precompiled = state::EthPrecompiles::tryDispatchInCall(codeAddress,
                    input.message, input.revisionConfig.revision, input.revisionConfig))
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
        state.checkpoint();
        if (input.extension != nullptr)
        {
            if (auto result = input.extension->tryChainPrecompile(
                    input.revisionConfig.revision, input.message))
            {
                output.result = evmc::Result(std::move(*result));
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
        }
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
        installCreatedContractCode(state, input.message, output.result.raw());
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
