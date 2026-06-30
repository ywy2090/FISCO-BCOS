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
 * @brief Chain policy hooks for the stateTransitionExecute pipeline (ADR-019).
 * @file StateTransitionHooks.h
 *
 * Kernel-neutral seam (ADR-005 Rule 1): `stateTransitionExecute` drives a fixed
 * transaction-level pipeline; chains inject policy through this virtual table instead
 * of branching inside `eth/pipeline/`.
 *
 * Scope spans **precheck through EVM entry** — not only pre-execution:
 *   - `onPreCheck*` hooks may set `ctx.earlyExit` before `innerExecute`
 *   - `onInvokeInnerExecute` is the default gateway into `innerExecute()` (full EVM)
 *
 * Paired symbols: `StateTransitionContext`, `StateTransitionErrorPolicy` (failure mapping),
 * `stateTransitionExecute`. Bound at each chain's `apply*Message` via `*OrchestrationProfile`.
 *
 * Implementations: `EthStateTransitionHooks`, `FiscoStateTransitionHooks`,
 * `OpStackStateTransitionHooks`.
 *
 * Related seams (different execution phase):
 *   - `EvmHostHooks` — inside `evm.Call` (SSTORE refund, value transfer, CREATE nonce)
 *   - `ChainExtendedPrecompileDispatch` — chain precompile classify/dispatch at CALL time
 *
 * See ADR-019, ADR-029, ADR-030 §6.
 */

#pragma once

#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/pipeline/DeductIntrinsicGas.h"
#include "bcos-evm/eth/pipeline/StateTransitionContext.h"

namespace bcos::evm
{

/// Chain policy for `stateTransitionExecute`. Hooks run in pipeline order below;
/// precheck hooks short-circuit via `ctx.earlyExit` (and usually `ctx.evmcResult`).
struct StateTransitionHooks
{
    virtual ~StateTransitionHooks() = default;

    /// Intrinsic-gas policy for the kernel `deductIntrinsicGas` step (configuration, not a hook).
    virtual DeductIntrinsicGasParams getIntrinsicGasParams() const = 0;

    /// Normalize `ctx.message` before any precheck (CREATE address derivation, deposit fields, …).
    virtual void onNormalizeMessage(StateTransitionContext& ctx) const { (void)ctx; }

    /// Entry rules: unsupported typed tx, authorization list, revision gates.
    /// Set `ctx.earlyExit` to reject before intrinsic debit.
    virtual void onPreCheckRules(StateTransitionContext& ctx) const { (void)ctx; }

    /// Gas-limit / pool affordability **before** intrinsic debit.
    /// Set `ctx.earlyExit` on rejection.
    virtual void onPreCheckGasAffordable(StateTransitionContext& ctx) const { (void)ctx; }

    /// Balance and top-level value transfer checks **after** intrinsic debit.
    /// Set `ctx.earlyExit` on rejection.
    virtual void onPreCheckCanTransfer(StateTransitionContext& ctx) const { (void)ctx; }

    /// Last-chance mutation of `InnerExecuteInput` after `ctx.toInnerExecuteInput()`.
    virtual void onTuneInnerExecuteInput(InnerExecuteInput& input) const { (void)input; }

    /// EVM entry point. Default runs `innerExecute()`; chains may wrap for tracing or policy.
    virtual InnerExecuteOutput onInvokeInnerExecute(InnerExecuteInput&& input) const
    {
        return innerExecute(std::move(input));
    }
};

// Pipeline order (stateTransitionExecute):
//   onNormalizeMessage → onPreCheckRules → onPreCheckGasAffordable
//   → deductIntrinsicGas(getIntrinsicGasParams())   [kernel step]
//   → onPreCheckCanTransfer
//   → toInnerExecuteInput → onTuneInnerExecuteInput → onInvokeInnerExecute
// Failure mapping: StateTransitionErrorPolicy::on* (not on this interface).

}  // namespace bcos::evm
