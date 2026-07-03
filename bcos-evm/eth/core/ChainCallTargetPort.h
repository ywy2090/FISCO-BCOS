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
 * @brief Chain-owned call target injection port (classify, dispatch, tx-entry warm).
 * @file ChainCallTargetPort.h
 *
 * Kernel-neutral seam (ADR-024): **chain-owned** CALL targets that are not Ethereum
 * `CallTargetRoute::BuiltinPrecompile`. The kernel routes via `classifyCallTarget` and
 * `executePrecompileEnvelope` without `#include` of `bcos/` or `opstack/`.
 *
 * Covers `CallTargetRoute::ChainPrecompile`:
 *   - FISCO: `0x1000+` precompiles and `[PRECOMPILED]` proxy accounts
 *   - OpStack: system predeploys (L1Block, GasPriceOracle, …)
 *
 * Eth reference execution passes `nullptr` (builtin precompiles only).
 *
 * Lifetime / wiring (one pointer per transaction, must outlive `innerExecute`):
 *   `apply*Message` / `*ExecutionBundle` → `StateTransitionContext::wireExecutionEnvironment`
 *                                      → `InnerExecuteInput::callTargetPort`
 *                                      → `innerExecute` / `runCallFrame`
 *                                      → `CallFrameContext::callTargetPort` (+ nested `EthHost`)
 *   Used by: `classifyCallTarget`, `executePrecompileEnvelope`, `enumerateTxEntryWarmTargets`
 *
 * Production adapters:
 *   - `OpStackChainCallTargetAdapter` — classify + dispatch + static warm (apply-local)
 *   - `FiscoChainCallTargetAdapter` — classify; dispatch delegates to TE
 * `ExecutorPrecompileAdapter`
 *
 * Frame policy (value transfer, DELEGATECALL gate, SSTORE) stays on `EvmHostHooks`.
 *
 * See ADR-024, ADR-027.
 */

#pragma once

#include "bcos-evm/eth/core/CallTargetTypes.h"
#include <evmc/evmc.h>
#include <functional>
#include <optional>

namespace bcos::evm::execution
{
enum class FrameScope;
}

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{

/// Chain extension port for `CallTargetRoute::ChainPrecompile` targets.
///
/// Three hooks mirror the kernel call-target pipeline:
///   1. `classifyTarget` — `classifyCallTarget` (before value transfer / VM)
///   2. `dispatch`       — `executePrecompileEnvelope` after classification
///   3. `forEachStaticWarmTarget` — `enumerateTxEntryWarmTargets` (EIP-2929 tx entry)
struct ChainCallTargetPort
{
    virtual ~ChainCallTargetPort() = default;

    /// Claim a chain-owned call target during routing.
    ///
    /// Invoked from `classifyCallTarget` when `(emptyCode || scope == Nested)` and
    /// `callTargetPort != nullptr`. Runs after EIP-7702 delegation routing and the
    /// `EvmHostHooks::allowDelegateCallToPrecompile` gate (builtin precompile only).
    ///
    /// `executionAddress` is the frame execution key from `routeFrameMessage`
    /// (not raw `msg.recipient` / `msg.code_address` when they differ). `msg` is the
    /// routed envelope (`ClassifiedCallTarget::routed`).
    ///
    /// @return Descriptor with `route == ChainPrecompile` and appropriate `AccessWarmSchedule`
    ///         when claimed; `std::nullopt` to fall through to builtin precompile, empty
    ///         account, or EVM contract resolution. May always return `nullopt` when
    ///         the port is dispatch-only (FISCO TE `ExecutorPrecompileAdapter`).
    virtual std::optional<execution::ClassifiedCallTarget> classifyTarget(state::State& state,
        evmc_address const& executionAddress, evmc_message const& msg,
        execution::FrameScope scope) = 0;

    /// Execute a target already classified as `CallTargetRoute::ChainPrecompile`.
    ///
    /// Called from `executePrecompileEnvelope`; must not re-classify or re-route.
    /// @param msg Final routed envelope (`ClassifiedCallTarget::routed`).
    /// @return `evmc_result` on success; `std::nullopt` → envelope reports precompile failure.
    virtual std::optional<evmc_result> dispatch(evmc_revision rev, evmc_message const& msg) = 0;

    /// Emit fixed chain addresses warmed at transaction entry (EIP-2929).
    ///
    /// Consumed by `enumerateTxEntryWarmTargets` alongside builtin precompiles from
    /// `PrecompileActive`. Emit only `AccessWarmSchedule::AtTxPrepareIfStatic` predeploys (OpStack
    /// L1Block / GasPriceOracle). Dynamic FISCO `[PRECOMPILED]` targets rely on
    /// `classifyTarget` at frame time; FISCO adapter leaves this as no-op.
    virtual void forEachStaticWarmTarget(
        std::function<void(evmc_address const&)> const& consume) const = 0;
};

}  // namespace bcos::evm
