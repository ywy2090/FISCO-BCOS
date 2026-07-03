/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Frame message routing: recipient/code_address normalization and EIP-7702 delegation.
 * @file FrameRouting.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include <evmc/evmc.h>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{
enum class FrameScope;

struct RoutedFrame
{
    evmc_message routed{};
    evmc_address executionAddress{};
};

/// Normalize routed message fields and derive executionAddress (geth: message routing pre-Call).
RoutedFrame routeFrameMessage(state::State& state, bcos::evm::RevisionConfig const& revisionConfig,
    evmc_message msg, FrameScope scope);

}  // namespace bcos::evm::execution
