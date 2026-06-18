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
 * @brief Transaction-entry warm set helper over state::State APIs.
 * @file warmTransactionEntry.h
 */

#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <cstring>

namespace bcos::evm::execution
{
namespace detail
{
inline evmc_address toEvmcAddress(const h160& value)
{
    evmc_address out{};
    std::memcpy(out.bytes, value.data(), sizeof(out.bytes));
    return out;
}

inline evmc_bytes32 toEvmcBytes32(const h256& value)
{
    evmc_bytes32 out{};
    std::memcpy(out.bytes, value.data(), sizeof(out.bytes));
    return out;
}
}  // namespace detail

inline void warmTransactionEntry(state::State& state, evmc_revision rev,
    const state::Transaction& tx, const state::BlockInfo& block,
    const state::TransactionProperties& props, const Eip2930AccessList* accessList = nullptr)
{
    (void)state.warm_up_address(tx.from);

    if (props.warmDestination && tx.to.has_value())
    {
        (void)state.warm_up_address(*tx.to);
    }

    if (props.warmCoinbase && rev >= EVMC_SHANGHAI)
    {
        (void)state.warm_up_address(block.coinbase);
    }

    if (accessList == nullptr || accessList->empty())
    {
        return;
    }

    for (auto const& [account, keys] : *accessList)
    {
        auto const address = detail::toEvmcAddress(account);
        (void)state.warm_up_address(address);
        for (auto const& key : keys)
        {
            (void)state.warm_up_storage(address, detail::toEvmcBytes32(key));
        }
    }
}
}  // namespace bcos::evm::execution
