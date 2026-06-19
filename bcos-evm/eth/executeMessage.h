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
 * @brief Top-level message execution over eth::state::EthHost.
 * @file executeMessage.h
 */

#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/policy/HostExtension.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.hpp>
#include <vector>

namespace bcos::evm
{
using LogEntry = state::LogEntry;

struct ExecuteMessageInput
{
    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    evmc_message message{};
    bcos::u256 gasPrice{0};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    state::TransactionProperties txProps{};
    const Eip2930AccessList* accessList{nullptr};
    uint8_t web3TypedTxKind{0};
    state::HostExtension* extension{nullptr};
    bool fixStorageStatus{true};
};

struct ExecuteMessageOutput
{
    evmc::Result result{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<LogEntry> logs;
};

ExecuteMessageOutput executeMessage(ExecuteMessageInput input);

}  // namespace bcos::evm
