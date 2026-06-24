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
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoOrchestrationInternals.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/eth/execution/TxFeaturePrepare.h"
#include "bcos-evm/eth/orchestration/OrchestrationPipeline.h"
#include "bcos-evm/eth/trace/EvmTrace.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <fmt/compile.h>
#include <fmt/format.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>

namespace bcos::evm
{
namespace
{
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
            topics.emplace_back(state::fromEvmC(topic));
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
        if (std::memcmp(message.code_address.bytes, EMPTY_EVM_ADDRESS.bytes,
                sizeof(EMPTY_EVM_ADDRESS.bytes)) == 0)
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
        auto salt = toBigEndian(state::fromEvmC(message.create2_salt));
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

    trace::EvmTraceScope traceScope(
        trace::makeTraceContext("fisco", input.blockInfo.number, input.txHash));

    ExecuteViaHostOutput output;
    output.executionContext.message = input.message;
    output.executionContext.revisionConfig = input.revisionConfig;

    auto const fixErrorHandling = input.revisionConfig.fix_error_handling;
    auto const eip7623Enabled = input.web3Tx && input.revisionConfig.eth().eip7623;

    OrchestrationContext ctx{
        *input.stateView, input.message, input.revisionConfig.eth(), input.gasPrice};
    ctx.inputs.vm = input.vm;
    ctx.inputs.hashImpl = input.hashImpl;
    ctx.inputs.blockInfo = input.blockInfo;
    ctx.inputs.blockHashes = input.blockHashes;
    ctx.inputs.accessList = input.accessList.get();
    ctx.inputs.authorizationListPresent = input.authorizationListPresent;
    ctx.inputs.authorizations = input.authorizations;
    ctx.inputs.web3TypedTxKind = input.web3TypedTxKind;

    trace::logMessageContext(input.message);

    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = &ctx.state;
    deps.blockNumber = input.blockInfo.number;
    deps.revisionFlags.fix_auth_check = input.revisionConfig.fix_auth_check;
    deps.revisionFlags.use_raw_address = input.revisionConfig.use_raw_address;
    deps.revisionFlags.fix_nonce_init = input.revisionConfig.fix_nonce_init;
    deps.revisionFlags.web3Tx = input.web3Tx;
    deps.hashImpl = input.hashImpl;
    deps.seq = input.nestedSeq;
    deps.origin = input.origin;
    deps.persistContractCreateNonce = std::move(input.persistContractCreateNonce);
    deps.recipientPathResolver = std::move(input.recipientPathResolver);
    deps.authPort = input.authPort;
    deps.chainPrecompilePort = input.chainPrecompilePort;
    FiscoHostExtension extension(input.revisionConfig.enable_balance_transfer, std::move(deps));
    ctx.extension = &extension;

    OrchestrationHooks hooks;
    hooks.prepareMessage = [&input](OrchestrationContext& orchestrationCtx) {
        orchestrationCtx.message = deriveMessage(FiscoTxAdapterInput{.web3Tx = input.web3Tx,
            .message = orchestrationCtx.message,
            .blockNumber = input.blockInfo.number,
            .contextID = input.contextID,
            .seq = input.seq,
            .nonce = input.nonce,
            .hashImpl = input.hashImpl});
    };

    hooks.preExecute = [&input](OrchestrationContext& orchestrationCtx) {
        if (input.revisionConfig.enable_auth_check && input.authPort != nullptr)
        {
            if (auto authResult =
                    const_cast<AuthPort*>(input.authPort)->checkAuth(orchestrationCtx.message);
                authResult.has_value())
            {
                orchestrationCtx.evmcResult = std::move(*authResult);
                orchestrationCtx.earlyExit = true;
                orchestrationCtx.exitKind = OrchestrationExitKind::PreExecuteRejected;
            }
        }
    };

    hooks.intrinsicPolicy.mode =
        eip7623Enabled ? IntrinsicDebitMode::Eip7623 : IntrinsicDebitMode::None;
    hooks.intrinsicPolicy.authorizationListPresent = input.authorizationListPresent;
    hooks.intrinsicPolicy.authTupleCount = input.authorizations.size();
    hooks.intrinsicPolicy.accessList = input.accessList.get();
    hooks.intrinsicPolicy.web3TypedTxKind = input.web3TypedTxKind;

    hooks.mapIntrinsicFailure = [fixErrorHandling, hashImpl = input.hashImpl](
                                    OrchestrationContext& orchestrationCtx,
                                    IntrinsicDebitFailure failure) {
        std::string reason = "EIP-7623 intrinsic OOG";
        switch (failure)
        {
        case IntrinsicDebitFailure::GasLimitMinimum:
            reason = "EIP-7623 gas limit minimum";
            break;
        case IntrinsicDebitFailure::CalldataOutOfGas:
            reason = "EIP-7623 calldata OOG";
            break;
        case IntrinsicDebitFailure::AuthTupleOutOfGas:
            reason = "EIP-7702 auth tuple OOG";
            break;
        default:
            break;
        }
        orchestrationCtx.evmcResult =
            makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS,
                fixErrorHandling ? 0 : orchestrationCtx.message.gas, reason, fixErrorHandling);
    };

    hooks.preKernel = [&input, eip7623Enabled](OrchestrationContext& orchestrationCtx) {
        if (input.revisionConfig.enable_balance_transfer)
        {
            maybeTransferValue(orchestrationCtx.state, orchestrationCtx.message,
                input.revisionConfig.fix_delegatecall_transfer);
        }

        if (!eip7623Enabled)
        {
            if (orchestrationCtx.message.gas < BALANCE_TRANSFER_GAS)
            {
                BOOST_THROW_EXCEPTION(protocol::OutOfGas{});
            }
            orchestrationCtx.message.gas -= BALANCE_TRANSFER_GAS;
        }

        if (!isCreateKind(orchestrationCtx.message.kind))
        {
            auto const code =
                orchestrationCtx.state.get_code(orchestrationCtx.message.code_address);
            if (code.empty() && orchestrationCtx.message.input_size > 0)
            {
                BOOST_THROW_EXCEPTION(NotFoundCodeError{});
            }
        }
    };

    hooks.tuneKernelInput = [&input](ExecuteMessageInput& executeInput) {
        executeInput.fixStorageStatus = input.revisionConfig.fix_storage_status;
        executeInput.fixNonceInit = input.revisionConfig.fix_nonce_init;
        executeInput.revisionConfig = input.revisionConfig.eth();
    };

    hooks.postAdopt = [](OrchestrationContext& orchestrationCtx) {
        if ((orchestrationCtx.message.kind == EVMC_CREATE ||
                orchestrationCtx.message.kind == EVMC_CREATE2) &&
            orchestrationCtx.evmcResult.status_code == EVMC_SUCCESS &&
            std::memcmp(orchestrationCtx.evmcResult.create_address.bytes, EMPTY_EVM_ADDRESS.bytes,
                sizeof(orchestrationCtx.evmcResult.create_address.bytes)) == 0)
        {
            orchestrationCtx.evmcResult.create_address = orchestrationCtx.message.recipient;
        }
    };

    hooks.postSettle = [fixRevertLogs = input.revisionConfig.fix_revert_logs](
                           OrchestrationContext& orchestrationCtx) {
        if (fixRevertLogs && orchestrationCtx.evmcResult.status_code != EVMC_SUCCESS)
        {
            orchestrationCtx.kernelOutput.logs.clear();
        }
    };

    hooks.mapException = [fixErrorHandling, hashImpl = input.hashImpl](
                             OrchestrationContext& c, std::exception_ptr exceptionPtr) {
        try
        {
            std::rethrow_exception(exceptionPtr);
        }
        catch (protocol::OutOfGas& e)
        {
            c.evmcResult = makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::OutOfGas,
                EVMC_OUT_OF_GAS, 0, e.what(), fixErrorHandling);
        }
        catch (protocol::NotEnoughCashError& e)
        {
            c.evmcResult = makeErrorEVMCResult(*hashImpl,
                protocol::TransactionStatus::NotEnoughCash, EVMC_INSUFFICIENT_BALANCE,
                fixErrorHandling ? 0 : c.message.gas, e.what(), fixErrorHandling);
        }
        catch (NotFoundCodeError&)
        {
            if ((c.message.flags & EVMC_STATIC) != 0 || c.message.kind == EVMC_DELEGATECALL)
            {
                c.evmcResult = makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::None,
                    EVMC_SUCCESS, c.message.gas, "", false);
            }
            else
            {
                c.evmcResult =
                    makeErrorEVMCResult(*hashImpl, protocol::TransactionStatus::RevertInstruction,
                        EVMC_REVERT, c.message.gas, "Call address error.", fixErrorHandling);
            }
        }
        catch (std::exception&)
        {
            c.evmcResult = makeErrorEVMCResult(*hashImpl,
                fixErrorHandling ? protocol::TransactionStatus::Unknown :
                                   protocol::TransactionStatus::OutOfGas,
                EVMC_INTERNAL_ERROR, fixErrorHandling ? 0 : c.message.gas, "", fixErrorHandling);
        }

        if (c.state.has_checkpoint())
        {
            c.state.revert();
        }
    };

    runOrchestration(ctx, hooks);

    EVM_LOG(DEBUG) << LOG_DESC("executeViaHost done")
                   << LOG_KV("exit", trace::exitKind(ctx.exitKind))
                   << LOG_KV("status", trace::evmcStatus(ctx.evmcResult.status_code))
                   << LOG_KV("gasLeft", ctx.evmcResult.gas_left) << LOG_KV("web3Tx", input.web3Tx);

    output.evmcResult = std::move(ctx.evmcResult);
    output.executionContext.logs = convertLogs(ctx.kernelOutput.logs);
    output.executionContext.message = ctx.message;

    if (hooks.intrinsicPolicy.mode == IntrinsicDebitMode::Eip7623)
    {
        output.executionContext.gasSettlementSnapshot = ctx.snapshot;
    }

    if (output.evmcResult.status_code == EVMC_SUCCESS)
    {
        output.stateDiff = std::move(ctx.kernelOutput.stateDiff);
    }
    else if (input.revisionConfig.fix_revert_logs)
    {
        output.executionContext.logs.clear();
    }

    if (output.evmcResult.gas_left < 0)
    {
        output.evmcResult =
            makeErrorEVMCResult(*input.hashImpl, protocol::TransactionStatus::OutOfGas,
                EVMC_OUT_OF_GAS, fixErrorHandling ? 0 : ctx.message.gas, "", fixErrorHandling);
    }

    co_return output;
}

}  // namespace bcos::evm
