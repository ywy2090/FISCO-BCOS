/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief Chain entry applyFiscoMessage.
 * @file ApplyFiscoMessage.h
 */

#pragma once

#include "bcos-evm/bcos/FiscoEvmHostHooks.h"
#include "bcos-evm/bcos/FiscoExecutionArtifacts.h"
#include "bcos-evm/bcos/FiscoRevisionConfig.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/eth/core/ChainExtendedPrecompileDispatch.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-task/Task.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <functional>
#include <memory>
#include <optional>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

struct FiscoExecutionRequest
{
    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};

    evmc_message message{};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::chain_policy::FiscoRevisionConfig revisionConfig{};
    bcos::u256 nonce{0};
    bcos::u256 gasPrice{0};
    int64_t contextID{0};
    int64_t seq{0};
    int64_t* nestedSeq{nullptr};
    evmc_address origin{};
    bool web3Tx{false};
    uint8_t web3TypedTxKind{0};
    std::shared_ptr<const Eip2930AccessList> accessList;
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;

    // Optional adapters for integration layering.
    AuthPort const* authPort{nullptr};
    ChainExtendedPrecompileDispatch* chainDispatchPort{nullptr};
    std::function<void(const evmc_address&, uint64_t)> persistContractCreateNonce;
    FiscoEvmHostHooks::RecipientPathResolver recipientPathResolver;
    std::optional<bcos::h256> txHash;
};

struct FiscoExecutionResult
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    FiscoExecutionArtifacts executionContext;
};

// ── Chain entry ───────────────────────────────────────────────────────────────
task::Task<FiscoExecutionResult> applyFiscoMessage(FiscoExecutionRequest input);

}  // namespace bcos::evm
