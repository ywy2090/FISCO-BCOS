/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Load executable bytecode for a routed frame (CREATE initcode or state code + 7702).
 * @file FrameBytecode.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

/// Load initcode (CREATE) or account bytecode, following EIP-7702 delegation when enabled.
bcos::bytes loadFrameBytecode(state::State& state, bcos::evm::RevisionConfig const& revisionConfig,
    evmc_message const& msg, evmc_address executionAddress);

}  // namespace bcos::evm::execution
