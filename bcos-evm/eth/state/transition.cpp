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
 * @file transition.cpp
 */

#include "bcos-evm/eth/state/transition.hpp"
#include "bcos-evm/eth/execution/warmTransactionEntry.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include <algorithm>

namespace bcos::evm::state
{
namespace
{
evmc_tx_context buildTxContext(const BlockInfo& block, const Transaction& tx) noexcept
{
    evmc_tx_context context{};
    context.tx_gas_price = toEvmC(tx.gasPrice);
    context.tx_origin = tx.from;
    context.block_coinbase = block.coinbase;
    context.block_number = block.number;
    context.block_timestamp = block.timestamp;
    context.block_gas_limit = block.gasLimit;
    context.block_prev_randao = block.prevRandao;
    context.chain_id = toEvmC(block.chainId);
    context.block_base_fee = toEvmC(block.baseFee);
    context.blob_base_fee = toEvmC(block.blobBaseFee);
    return context;
}

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
    if (!tx.to.has_value())
    {
        msg.input_data = nullptr;
        msg.input_size = 0;
    }
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

TransactionReceipt transition(const StateView& state_view, const BlockInfo& block,
    const BlockHashes& block_hashes, const Transaction& tx, evmc_revision rev, evmc::VM& vm,
    const TransactionProperties& tx_props, HostExtension* ext)
{
    TransactionReceipt receipt{};
    State state(state_view);

    execution::warmTransactionEntry(state, rev, tx, block, tx_props);

    if (tx.to.has_value())
    {
        auto const precompile_result = EthPrecompiles::dispatch(
            *tx.to, bcos::bytesConstRef(tx.data.data(), tx.data.size()), tx.gasLimit, rev);
        if (precompile_result.has_value())
        {
            receipt.status = precompile_result->status;
            receipt.output = precompile_result->output;
            if (receipt.status == EVMC_OUT_OF_GAS)
            {
                receipt.gasUsed = tx.gasLimit;
            }
            else
            {
                receipt.gasUsed = std::clamp<int64_t>(precompile_result->gasCost, 0, tx.gasLimit);
            }
            return receipt;
        }
    }

    auto const tx_context = buildTxContext(block, tx);
    EthHost host(state, tx_context, rev, vm, block_hashes, ext);
    auto msg = buildTopLevelMessage(tx, tx_props);

    bcos::bytes code;
    if (tx.to.has_value())
    {
        code = state.get_code(*tx.to);
    }
    else
    {
        code = tx.data;
    }

    state.checkpoint();
    auto const result = vm.execute(host, rev, msg, code.data(), code.size());
    receipt.status = result.status_code;
    receipt.output = resultOutputToBytes(result);
    receipt.gasUsed = calcGasUsed(tx.gasLimit, result.gas_left);
    receipt.gasRefund = std::max<int64_t>(0, result.gas_refund);

    if (result.status_code == EVMC_SUCCESS)
    {
        state.commit();
    }
    else
    {
        state.revert();
    }

    return receipt;
}
}  // namespace bcos::evm::state
