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
 * @file Transition.cpp
 */

#include "helpers/Transition.hpp"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include <algorithm>

namespace bcos::evm::state
{
namespace
{
evmc_message buildTopLevelMessage(
    const Transaction& tx, const TransactionProperties& tx_props) noexcept
{
    evmc_message msg{};
    msg.kind = tx.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    msg.flags = tx_props.isStatic ? EVMC_STATIC : 0;
    msg.depth = 0;
    msg.gas = tx.gasLimit;
    msg.recipient = tx.to.value_or(evmc_address{});
    msg.sender = tx.from;
    msg.input_data = tx.data.data();
    msg.input_size = tx.data.size();
    msg.value = toEvmC(tx.value);
    msg.create2_salt = {};
    msg.code_address = msg.recipient;
    return msg;
}

bcos::bytes resultOutputToBytes(const evmc::Result& result)
{
    if (result.output_data == nullptr || result.output_size == 0)
    {
        return {};
    }
    return bcos::bytes(result.output_data, result.output_data + result.output_size);
}

int64_t calcGasUsed(int64_t gas_limit, int64_t gas_left) noexcept
{
    auto const clamped_left = std::max<int64_t>(gas_left, 0);
    return std::clamp<int64_t>(gas_limit - clamped_left, 0, gas_limit);
}
}  // namespace

TransactionReceipt transition(const EvmStateReader& state_view, const BlockInfo& block,
    const BlockHashes& block_hashes, const Transaction& tx, evmc_revision rev, evmc::VM& vm,
    const TransactionProperties& tx_props, VmHostPolicy* ext)
{
    TransactionReceipt receipt{};
    auto msg = buildTopLevelMessage(tx, tx_props);
    // Keep transition() as a thin adapter: warm-up + execution + commit/revert
    // are centralized in executeMessage().
    State state(state_view);
    auto executeOutput = executeMessage(ExecuteMessageInput{.state = &state,
        .vm = &vm,
        .message = msg,
        .gasPrice = tx.gasPrice,
        .blockInfo = block,
        .blockHashes = block_hashes,
        .revisionConfig = bcos::evm_standard::RevisionConfig{.revision = rev},
        .txProps = tx_props,
        .extension = ext});

    receipt.status = executeOutput.result.status_code;
    receipt.output = resultOutputToBytes(executeOutput.result);
    receipt.gasUsed = calcGasUsed(tx.gasLimit, executeOutput.result.gas_left);
    receipt.gasRefund = std::max<int64_t>(0, executeOutput.result.gas_refund);

    return receipt;
}
}  // namespace bcos::evm::state
