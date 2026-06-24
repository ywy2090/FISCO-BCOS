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
 * @file FiscoPipelineHookBinder.cpp
 */

#include "bcos-evm/bcos/FiscoPipelineHookBinder.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoPipelineInternals.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/eth/orchestration/TxPipelineContext.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <boost/throw_exception.hpp>
#include <cstring>

namespace bcos::evm
{

TxPipelineHooks FiscoPipelineHookBinder::buildHooks(HookBindingContext& session)
{
    auto& input = session.input;
    TxPipelineHooks hooks;

    hooks.txSetupMessage = [&input](TxPipelineContext& orchestrationCtx) {
        orchestrationCtx.message = deriveMessage(FiscoTxAdapterInput{.web3Tx = input.web3Tx,
            .message = orchestrationCtx.message,
            .blockNumber = input.blockInfo.number,
            .contextID = input.contextID,
            .seq = input.seq,
            .nonce = input.nonce,
            .hashImpl = input.hashImpl});
    };

    hooks.txCheckTransactionRules = [&input](TxPipelineContext& orchestrationCtx) {
        if (input.revisionConfig.enable_auth_check && input.authPort != nullptr)
        {
            if (auto authResult =
                    const_cast<AuthPort*>(input.authPort)->checkAuth(orchestrationCtx.message);
                authResult.has_value())
            {
                orchestrationCtx.evmcResult = std::move(*authResult);
                orchestrationCtx.earlyExit = true;
                orchestrationCtx.exitKind = TxPipelineExitKind::RulesRejected;
            }
        }
    };

    hooks.intrinsicPolicy.mode =
        session.eip7623Enabled ? IntrinsicDebitMode::Eip7623 : IntrinsicDebitMode::None;
    hooks.intrinsicPolicy.authorizationListPresent = input.authorizationListPresent;
    hooks.intrinsicPolicy.authTupleCount = input.authorizations.size();
    hooks.intrinsicPolicy.accessList = input.accessList.get();
    hooks.intrinsicPolicy.web3TypedTxKind = input.web3TypedTxKind;

    hooks.txCheckBalanceAndValue = [&input, eip7623Enabled = session.eip7623Enabled](
                                       TxPipelineContext& orchestrationCtx) {
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

    hooks.txTuneExecutionInput = [&input](ExecuteMessageInput& executeInput) {
        executeInput.fixStorageStatus = input.revisionConfig.fix_storage_status;
        executeInput.fixNonceInit = input.revisionConfig.fix_nonce_init;
        executeInput.revisionConfig = input.revisionConfig.eth();
    };

    hooks.txPatchExecutionResult = [](TxPipelineContext& orchestrationCtx) {
        if ((orchestrationCtx.message.kind == EVMC_CREATE ||
                orchestrationCtx.message.kind == EVMC_CREATE2) &&
            orchestrationCtx.evmcResult.status_code == EVMC_SUCCESS &&
            std::memcmp(orchestrationCtx.evmcResult.create_address.bytes, EMPTY_EVM_ADDRESS.bytes,
                sizeof(orchestrationCtx.evmcResult.create_address.bytes)) == 0)
        {
            orchestrationCtx.evmcResult.create_address = orchestrationCtx.message.recipient;
        }
    };

    hooks.txFinalizeGasSettlement = [fixRevertLogs = input.revisionConfig.fix_revert_logs](
                                        TxPipelineContext& orchestrationCtx) {
        if (fixRevertLogs && orchestrationCtx.evmcResult.status_code != EVMC_SUCCESS)
        {
            orchestrationCtx.kernelOutput.logs.clear();
        }
    };

    return hooks;
}

}  // namespace bcos::evm
