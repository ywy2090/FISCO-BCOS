/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Frame-level execution target resolution (7702 / CREATE address normalization).
 * @file FrameTargetResolver.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include <evmc/evmc.h>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

struct FrameTarget
{
    evmc_message routed{};
    evmc_address executionAddress{};
};

FrameTarget resolveFrameTarget(state::State& state,
    bcos::evm_standard::RevisionConfig const& revisionConfig, evmc_message msg, FrameScope scope);

}  // namespace bcos::evm::execution
