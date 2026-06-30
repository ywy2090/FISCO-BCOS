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
 * @brief Chain-owned precompile seam (classify, dispatch, tx-entry warm).
 * @file ChainPrecompileDispatch.h
 *
 * Kernel-neutral injection port (ADR-024, ADR-005 Rule 1): `eth/` classifies and
 * routes CALL/STATICCALL via `execution::resolveCallTarget`, but chain-specific
 * predeploys and precompiles (FISCO small-address / `[PRECOMPILED]`, OpStack
 * L1Block / GasPriceOracle, …) are implemented outside `eth/`.
 *
 * Wired per transaction: `apply*Message` → `StateTransitionContext::chainPort` →
 * `FrameExecutionEnv::chainPort` → `EvmCallFrame` / `PrecompileRouter`.
 * Eth reference path passes `nullptr` (builtin precompiles only).
 *
 * Implementations: `FiscoChainCallTargetAdapter`, `OpStackChainCallTargetAdapter`.
 * See ADR-024 §3 and ADR-030 §6.
 */

#pragma once

#include "bcos-evm/eth/execution/CallTargetResolver.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm
{

/// Chain extension for non-builtin call targets. Classification pairs with
/// `execution::resolveCallTarget`; execution pairs with `executePrecompileEnvelope`.
struct ChainPrecompileDispatch
{
    virtual ~ChainPrecompileDispatch() = default;

    /// Chain hook during classification. Called when account code is empty or the
    /// frame is nested (`emptyCode || scope == Nested`). Return a descriptor with
    /// `kind == ChainPrecompile` (and `WarmPolicy`) when this chain owns the target;
    /// `std::nullopt` lets the kernel fall through to builtin precompile / EVM / empty.
    virtual std::optional<execution::CallTargetDescriptor> classifyTarget(state::State& state,
        evmc_address const& executionAddress, evmc_message const& msg,
        execution::FrameScope scope) = 0;

    /// Execute a target already classified as `CallTargetKind::ChainPrecompile`.
    /// `msg` is the routed envelope from `CallTargetDescriptor::routed`.
    virtual std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) = 0;

    /// Emit chain-owned addresses that must be warm at transaction entry (EIP-2929).
    /// Consumed by `execution::enumerateTxEntryWarmTargets` alongside builtin precompiles.
    virtual void forEachStaticWarmTarget(
        std::function<void(evmc_address const&)> const& consume) const = 0;
};

}  // namespace bcos::evm
