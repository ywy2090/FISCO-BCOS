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
 * @file ExecuteViaHost.cpp
 */

#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/gas/Eip7623.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <fmt/compile.h>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

EVMCResult adoptResult(evmc::Result&& result, const bcos::crypto::Hash& hashImpl)
{
    auto raw = result.release_raw();
    auto [status, ignored] = evmcStatusToErrorMessage(hashImpl, raw.status_code);
    (void)ignored;
    return EVMCResult(raw, status);
}

std::vector<protocol::LogEntry> convertLogs(const std::vector<state::LogEntry>& logs)
{
    std::vector<protocol::LogEntry> out;
    out.reserve(logs.size());
    for (auto const& log : logs)
    {
        std::span addressView(log.address.bytes, sizeof(log.address.bytes));
        h256s topics;
        topics.reserve(log.topics.size());
        for (auto const& topic : log.topics)
        {
            topics.emplace_back(fromEvmC(topic));
        }
        out.emplace_back(
            toHex<decltype(addressView), bcos::bytes>(addressView), std::move(topics), log.data);
    }
    return out;
}
}  // namespace

evmc_message deriveMessage(const FiscoTxAdapterInput& input)
{
    if (input.hashImpl == nullptr)
    {
        return input.message;
    }

    auto message = input.message;
    switch (message.kind)
    {
    case EVMC_CREATE:
    {
        if (concepts::bytebuffer::equalTo(
                message.code_address.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
        {
            if (!input.web3Tx)
            {
                auto address = fmt::format(
                    FMT_COMPILE("{}_{}_{}"), input.blockNumber, input.contextID, input.seq);
                auto hash = input.hashImpl->hash(address);
                std::copy_n(
                    hash.data(), sizeof(message.code_address.bytes), message.code_address.bytes);
            }
            else
            {
                auto legacyAddr =
                    newLegacyEVMAddress(bytesConstRef(message.sender.bytes), input.nonce);
                std::copy(legacyAddr.begin(), legacyAddr.end(), message.code_address.bytes);
            }
        }
        message.recipient = message.code_address;
        break;
    }
    case EVMC_CREATE2:
    {
        std::array<bcos::byte, 1 + sizeof(message.sender.bytes) + sizeof(message.create2_salt) +
                                   crypto::HashType::SIZE>
            buffer;
        uint8_t* ptr = buffer.data();
        *ptr++ = 0xff;
        ptr = std::uninitialized_copy_n(message.sender.bytes, sizeof(message.sender.bytes), ptr);
        auto salt = toBigEndian(fromEvmC(message.create2_salt));
        ptr = std::uninitialized_copy(salt.begin(), salt.end(), ptr);
        auto inputHash =
            input.hashImpl->hash(bytesConstRef(message.input_data, message.input_size));
        ptr = std::uninitialized_copy(inputHash.begin(), inputHash.end(), ptr);
        auto addressHash = input.hashImpl->hash(bytesConstRef(buffer.data(), buffer.size()));
        std::copy_n(addressHash.begin() + 12, sizeof(message.code_address.bytes),
            message.code_address.bytes);
        message.recipient = message.code_address;
        break;
    }
    default:
        break;
    }

    return message;
}

task::Task<ExecuteViaHostOutput> executeViaHost(ExecuteViaHostInput input)
{
    if (input.stateView == nullptr || input.vm == nullptr || input.hashImpl == nullptr)
    {
        throw std::invalid_argument("executeViaHost requires stateView/vm/hashImpl");
    }

    auto message = deriveMessage(FiscoTxAdapterInput{.web3Tx = input.web3Tx,
        .message = input.message,
        .blockNumber = input.blockInfo.number,
        .contextID = input.contextID,
        .seq = input.seq,
        .nonce = input.nonce,
        .hashImpl = input.hashImpl});

    ExecuteViaHostOutput output;
    output.executionContext.message = message;
    output.executionContext.revisionConfig = input.revisionConfig;

    state::State state(*input.stateView);
    state::TransactionProperties txProps;
    txProps.warmDestination = !isCreateKind(message.kind);

    auto const fixErrorHandling = input.revisionConfig.fix_error_handling;

    try
    {
        if (input.revisionConfig.enable_auth_check && input.authChecker)
        {
            if (auto authResult = input.authChecker(message); authResult.has_value())
            {
                output.evmcResult = std::move(*authResult);
                co_return output;
            }
        }

        if (input.web3Tx && input.revisionConfig.eth().eip7623)
        {
            auto const components =
                gas::calcEip7623Components(bytesConstRef(message.input_data, message.input_size));
            if (message.gas < components.normalCost)
            {
                output.evmcResult = makeErrorEVMCResult(*input.hashImpl,
                    protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS,
                    fixErrorHandling ? 0 : message.gas, "EIP-7623 calldata OOG", fixErrorHandling);
                co_return output;
            }
            message.gas -= components.normalCost;
        }

        if (input.web3Tx && input.revisionConfig.eth().eip7623)
        {
            auto const intrinsic =
                gas::computeTxIntrinsicGas(message, input.accessList.get(), input.web3TypedTxKind);
            output.executionContext.gasSettlementSnapshot.gasLimit = input.message.gas;
            output.executionContext.gasSettlementSnapshot.gasBeforeEvm = message.gas;
            output.executionContext.gasSettlementSnapshot.calldata =
                gas::calcEip7623Components(bytesConstRef(message.input_data, message.input_size));
            output.executionContext.gasSettlementSnapshot.fixedIntrinsic = intrinsic.fixedCost();
            output.executionContext.gasSettlementSnapshot.createTerm = intrinsic.createIntrinsic;
        }

        FiscoHostExtension::FiscoHostExtensionDeps deps;
        deps.state = &state;
        deps.blockNumber = input.blockInfo.number;
        deps.revisionFlags.fix_auth_check = input.revisionConfig.fix_auth_check;
        deps.revisionFlags.use_raw_address = input.revisionConfig.use_raw_address;
        deps.revisionFlags.fix_nonce_init = input.revisionConfig.fix_nonce_init;
        deps.revisionFlags.web3Tx = input.web3Tx;
        deps.revisionFlags.createLevel = message.depth;
        deps.recipientPathResolver = std::move(input.recipientPathResolver);
        deps.createAuthTableInvoker = std::move(input.createAuthTableInvoker);
        FiscoHostExtension extension(
            input.revisionConfig.enable_balance_transfer, std::move(deps), input.precompileCaller);

        auto executeOutput = executeMessage(ExecuteMessageInput{.stateView = &state,
            .vm = input.vm,
            .message = message,
            .gasPrice = input.gasPrice,
            .blockInfo = input.blockInfo,
            .blockHashes = input.blockHashes,
            .revisionConfig = input.revisionConfig.eth(),
            .txProps = txProps,
            .accessList = input.accessList.get(),
            .web3TypedTxKind = input.web3TypedTxKind,
            .extension = &extension,
            .fixStorageStatus = input.revisionConfig.fix_storage_status});

        output.evmcResult = adoptResult(std::move(executeOutput.result), *input.hashImpl);
        output.executionContext.logs = convertLogs(executeOutput.logs);
        output.executionContext.message = message;

        if (output.evmcResult.status_code == EVMC_SUCCESS)
        {
            output.stateDiff = std::move(executeOutput.stateDiff);
        }
        else
        {
            if (input.revisionConfig.fix_revert_logs)
            {
                output.executionContext.logs.clear();
            }
        }
    }
    catch (protocol::OutOfGas& e)
    {
        output.evmcResult = makeErrorEVMCResult(*input.hashImpl,
            protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, 0, e.what(), fixErrorHandling);
        if (state.has_checkpoint())
        {
            state.revert();
        }
    }
    catch (protocol::NotEnoughCashError& e)
    {
        output.evmcResult = makeErrorEVMCResult(*input.hashImpl,
            protocol::TransactionStatus::NotEnoughCash, EVMC_INSUFFICIENT_BALANCE,
            fixErrorHandling ? 0 : message.gas, e.what(), fixErrorHandling);
        if (state.has_checkpoint())
        {
            state.revert();
        }
    }
    catch (std::exception&)
    {
        output.evmcResult = makeErrorEVMCResult(*input.hashImpl,
            fixErrorHandling ? protocol::TransactionStatus::Unknown :
                               protocol::TransactionStatus::OutOfGas,
            EVMC_INTERNAL_ERROR, fixErrorHandling ? 0 : message.gas, "", fixErrorHandling);
        if (state.has_checkpoint())
        {
            state.revert();
        }
    }

    if (output.evmcResult.gas_left < 0)
    {
        output.evmcResult =
            makeErrorEVMCResult(*input.hashImpl, protocol::TransactionStatus::OutOfGas,
                EVMC_OUT_OF_GAS, fixErrorHandling ? 0 : message.gas, "", fixErrorHandling);
    }

    co_return output;
}

}  // namespace bcos::evm
