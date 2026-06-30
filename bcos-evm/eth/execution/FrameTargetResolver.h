/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Frame-level execution target and bytecode resolution (7702 / CREATE).
 * @file FrameTargetResolver.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>

namespace bcos::evm::execution
{

struct FrameTarget
{
    evmc_message routed{};
    evmc_address executionAddress{};
};

FrameTarget resolveFrameTarget(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message msg, FrameScope scope);

bcos::bytes resolveExecutionCode(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message const& msg,
    evmc_address executionAddress);

}  // namespace bcos::evm::execution
