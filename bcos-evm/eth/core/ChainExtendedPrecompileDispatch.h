/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief Chain-extended precompile injection port (classify, dispatch, tx-entry warm).
 * @file ChainExtendedPrecompileDispatch.h
 *
 * Kernel-neutral seam for **chain-owned** call targets that are not Ethereum builtin
 * precompiles. The `eth/` kernel resolves CALL/STATICCALL via
 * `execution::resolveCallTarget` and routes `CallTargetKind::BuiltinPrecompile` through
 * `PrecompileRouter` without knowing FISCO or OpStack details.
 *
 * This port covers the complementary `CallTargetKind::ChainPrecompile` path:
 *   - FISCO small-address precompiles and `[PRECOMPILED]` proxy accounts
 *   - OpStack system predeploys (e.g. L1Block, GasPriceOracle)
 *
 * Eth reference execution passes `nullptr` for `chainPort` (builtin precompiles only).
 *
 * Lifetime / wiring (one pointer per transaction, shared at nested depth):
 *   `apply*Message` → `StateTransitionContext::chainPort`
 *                   → `FrameExecutionEnv::chainPort`
 *                   → `resolveCallTarget` / `executePrecompileEnvelope` / `warmTransactionEntry`
 *
 * Implementations: `FiscoChainCallTargetAdapter`, `OpStackChainCallTargetAdapter`.
 * Host policy hooks (value transfer, SSTORE, CREATE nonce) stay on `EvmHostHooks`.
 */

#pragma once

#include "bcos-evm/eth/core/CallTargetKind.h"
#include "bcos-evm/eth/core/FrameScope.h"
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{

/// Chain extension port for non-builtin precompile targets.
///
/// Three hooks mirror the kernel call-target pipeline:
///   1. `classifyTarget` — during `resolveCallTarget` (before value transfer / VM)
///   2. `dispatch`       — during `executePrecompileEnvelope` for `ChainPrecompile`
///   3. `forEachStaticWarmTarget` — during `enumerateTxEntryWarmTargets` (EIP-2929 tx entry)
struct ChainExtendedPrecompileDispatch
{
    virtual ~ChainExtendedPrecompileDispatch() = default;

    /// Classify whether this chain owns the call target.
    ///
    /// Invoked from `resolveCallTarget` when `emptyCode || scope == Nested` and `chainPort != nullptr`.
    /// `executionAddress` is the frame execution key from `resolveFrameTarget` (not raw
    /// `msg.recipient` / `msg.code_address` when they disagree).
    ///
    /// @return A descriptor with `kind == ChainPrecompile`, `dispatchAddress`, and `WarmPolicy`
    ///         when the chain claims the target; `std::nullopt` to fall through to builtin
    ///         precompile / empty account / EVM contract resolution in the kernel.
    virtual std::optional<execution::CallTargetDescriptor> classifyTarget(state::State& state,
        evmc_address const& executionAddress, evmc_message const& msg,
        execution::FrameScope scope) = 0;

    /// Execute a target already classified as `CallTargetKind::ChainPrecompile`.
    ///
    /// Called from `executePrecompileEnvelope` after classification; must not re-route.
    /// @param msg Routed envelope (`CallTargetDescriptor::routed`); gas and addresses are final.
    /// @return `evmc_result` on success; `std::nullopt` if the adapter declines (kernel treats as failure).
    virtual std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) = 0;

    /// Enumerate chain-owned addresses that must be warm at transaction entry (EIP-2929).
    ///
    /// Consumed by `enumerateTxEntryWarmTargets` alongside builtin precompiles from
    /// `PrecompileActive`. Emit only fixed predeploys; dynamic targets (e.g. FISCO `[PRECOMPILED]`)
    /// rely on `classifyTarget` at frame time instead.
    virtual void forEachStaticWarmTarget(
        std::function<void(evmc_address const&)> const& consume) const = 0;
};

}  // namespace bcos::evm
