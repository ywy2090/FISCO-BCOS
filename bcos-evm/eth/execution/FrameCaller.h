/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Resolve effective caller address for an execution frame.
 * @file FrameCaller.h
 */

#pragma once

#include "bcos-evm/eth/state/HashUtils.hpp"
#include <evmc/evmc.h>

namespace bcos::evm::execution
{

/// Nested frame caller: execution context address when set, else message sender (DELEGATECALL).
inline evmc_address resolveCallerAddress(
    evmc_address const& executionAddress, evmc_message const& msg) noexcept
{
    if (!state::isZeroAddress(executionAddress))
    {
        return executionAddress;
    }
    return msg.sender;
}

}  // namespace bcos::evm::execution
