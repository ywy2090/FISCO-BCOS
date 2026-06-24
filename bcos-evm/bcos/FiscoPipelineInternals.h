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
 * @file FiscoPipelineInternals.h
 */

#pragma once

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <cstring>

namespace bcos::evm
{

struct NotFoundCodeError : public std::runtime_error
{
    NotFoundCodeError() : std::runtime_error("code not found") {}
};

inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

namespace detail
{
inline bool hasNonZeroValue(evmc_bytes32 const& value)
{
    for (auto byte : value.bytes)
    {
        if (byte != 0)
        {
            return true;
        }
    }
    return false;
}

inline bool addressEqual(evmc_address const& lhs, evmc_address const& rhs) noexcept
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}
}  // namespace detail

inline void maybeTransferValue(
    state::State& state, evmc_message const& msg, bool fixDelegateCallTransfer)
{
    if (!detail::hasNonZeroValue(msg.value))
    {
        return;
    }

    // CREATE/CREATE2 endowment is applied by the EVM kernel; pre-transfer would double-count.
    if (isCreateKind(msg.kind))
    {
        return;
    }

    bool const shouldTransfer = fixDelegateCallTransfer ?
                                    ((msg.kind == EVMC_CALL && (msg.flags & EVMC_STATIC) == 0)) :
                                    true;
    if (!shouldTransfer)
    {
        return;
    }

    if (detail::addressEqual(msg.code_address, msg.sender) ||
        detail::addressEqual(msg.recipient, msg.sender))
    {
        return;
    }

    auto const value = state::fromEvmC(msg.value);
    auto const fromBalance = state.get_balance(msg.sender);
    if (fromBalance < value)
    {
        BOOST_THROW_EXCEPTION(
            protocol::NotEnoughCashError{} << errinfo_comment("Account balance is not enough!"));
    }
    auto const toBalance = state.get_balance(msg.recipient);
    state.set_balance(msg.sender, fromBalance - value);
    state.set_balance(msg.recipient, toBalance + value);
}

}  // namespace bcos::evm
