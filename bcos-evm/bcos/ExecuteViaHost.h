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
 * @brief executeViaHost orchestration layer.
 * @file ExecuteViaHost.h
 */

#pragma once

#include "bcos-evm/bcos/FiscoExecutionContext.h"
#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "bcos-evm/bcos/FiscoRevisionConfig.h"
#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-task/Task.h"
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

struct ExecuteViaHostInput
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
    bool web3Tx{false};
    uint8_t web3TypedTxKind{0};
    std::shared_ptr<const Eip2930AccessList> accessList;

    // Optional adapters for integration layering.
    std::function<std::optional<EVMCResult>(const evmc_message&)> authChecker;
    FiscoHostExtension::FiscoPrecompileCaller precompileCaller;
    FiscoHostExtension::CreateAuthTableInvoker createAuthTableInvoker;
    FiscoHostExtension::RecipientPathResolver recipientPathResolver;
};

struct ExecuteViaHostOutput
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    FiscoExecutionContext executionContext;
};

task::Task<ExecuteViaHostOutput> executeViaHost(ExecuteViaHostInput input);

}  // namespace bcos::evm
