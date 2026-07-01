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
 * @brief In-call host policy hooks inside evm.Call (ADR-005 / ADR-027).
 * @file EvmHostHooks.h
 *
 * Kernel-neutral seam (ADR-005 Rule 1): chains inject in-frame semantics through this
 * virtual table instead of branching inside `eth/kernel/execution/` or `eth/host/`.
 *
 * Scope spans **inside evm.Call / runCallFrame** — after `stateTransitionExecute` entry:
 *   - SSTORE refund + storage status (`EthHost::set_storage`)
 *   - SELFDESTRUCT gate (`EthHost::selfdestruct`)
 *   - CALL value transfer skip (`transferFrameValue`, precompile envelopes)
 *   - DELEGATECALL-to-precompile policy (`resolveCallTarget`)
 *   - nested CREATE message prep + nonce finalization (`prepareNestedMessage`,
 *     `bumpNestedCreateSenderNonce`, `finalizeFrame`)
 *
 * Paired symbols: `FrameExecutionEnv::extension`, `EthHost::m_extension`,
 * `applySstoreRefundEip3529`, `classifyStorageStatusPrecise`. Wired at each chain's
 * `apply*Message` or execution bundle (e.g. `ApplyEthMessage`, `FiscoExecutionBundle`).
 *
 * Implementations: `EthEvmHostHooks` (pure-ethereum defaults), `FiscoEvmHostHooks`
 * (legacy SSTORE / nonce / auth). OpStack passes `nullptr` (base-class defaults).
 *
 * Lifetime / wiring (one pointer per transaction, shared at nested depth):
 *   `apply*Message` / `*ExecutionBundle` → `StateTransitionContext::wireExecutionEnvironment`
 *                                       → `InnerExecuteInput::extension`
 *                                       → `FrameExecutionEnv::extension`
 *                                       → `EthHost` / `EvmCallFrame`
 *
 * Related seams (different execution phase):
 *   - `StateTransitionHooks` — tx-level precheck through `innerExecute` entry
 *   - `ChainExtendedPrecompileDispatch` — chain precompile classify/dispatch at CALL time
 *
 * See ADR-005, ADR-027, ADR-030 §6.
 */

#pragma once

#include <evmc/evmc.h>

namespace bcos::evm::state
{
struct Account;
class State;

/// EIP-3529 SSTORE refund helper shared by default `EvmHostHooks` and FISCO when
/// `RevisionFlags::fix_storage_status` is enabled (standard Ethereum semantics).
void applySstoreRefundEip3529(State& state, evmc_bytes32 const& current,
    evmc_bytes32 const& original, evmc_bytes32 const& newValue) noexcept;

/// EIP-2200 / EIP-3529 storage status helper shared by default `EvmHostHooks` and FISCO when
/// `RevisionFlags::fix_storage_status` is enabled (evmone precise mapping).
evmc_storage_status classifyStorageStatusPrecise(evmc_bytes32 const& original,
    evmc_bytes32 const& current, evmc_bytes32 const& newValue) noexcept;

/// Injectable hooks for EthHost extension points inside evm.Call.
/// Chain precompile dispatch is via `ChainExtendedPrecompileDispatch` on FrameExecutionEnv.
struct EvmHostHooks
{
    virtual ~EvmHostHooks() = default;

    /// SELFDESTRUCT gate inside `EthHost::selfdestruct`. Default allows destruction.
    /// FISCO returns `false`; ETH reference may disable per audit policy.
    virtual bool allowSelfdestruct(const Account& acc) { return true; }

    /// DELEGATECALL-to-precompile gate in `resolveCallTarget`.
    /// When `false`, active empty-code precompile targets return `PolicyRejected`.
    virtual bool allowDelegateCallToPrecompile() { return true; }

    /// Skip native value transfer in `transferFrameValue` and precompile envelopes.
    /// FISCO uses when value was moved in orchestration (ADR-005).
    virtual bool skipHostValueTransfer() { return false; }

    /// Last-chance `evmc_message` mutation before nested frame VM entry (`prepareNestedMessage`).
    /// FISCO: CREATE address derivation, auth table routing, deposit fields.
    virtual void prepareMessage(evmc_revision rev, evmc_message& msg)
    {
        (void)rev;
        (void)msg;
    }

    /// Persist resolved caller for chain-specific host logic (`prepareNestedMessage`).
    /// FISCO uses for auth / `[PRECOMPILED]` recipient path resolution.
    virtual void setCallerAddress(const evmc_address& caller) { (void)caller; }

    /// Extra nonce semantics after nested CREATE sender bump (`bumpNestedCreateSenderNonce`).
    /// FISCO persists contract-create nonce when sender differs from tx origin.
    virtual void bumpContractCreateNonce(const evmc_address& contractAddress)
    {
        (void)contractAddress;
    }

    /// EIP-3529 SSTORE refund accounting before slot write (`EthHost::set_storage`).
    /// Default delegates to `applySstoreRefundEip3529`; FISCO may use legacy matrix when
    /// `fix_storage_status` is off.
    virtual void applySstoreRefund(State& state, evmc_bytes32 const& current,
        evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept;

    /// EIP-2200 storage status returned to evmone after `set_storage`.
    /// Default uses `classifyStorageStatusPrecise`; FISCO overrides for legacy status matrix.
    virtual evmc_storage_status classifyStorageStatus(evmc_bytes32 const& original,
        evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept;

    /// Post-classification refund adjustment for legacy deleted slots (`EthHost::set_storage`).
    /// No-op by default; FISCO applies when `fix_storage_status` is off.
    virtual void applyLegacySstoreDeletedRefund(
        State& state, evmc_storage_status status) const noexcept;

    /// Top-level CREATE nonce finalization after successful code install (`finalizeFrame`).
    /// No-op by default; FISCO uses when `fix_nonce_init` / contract-create nonce is enabled.
    virtual void finalizeTopLevelCreateNonce(State& state, evmc_address const& createAddr) noexcept;
};
}  // namespace bcos::evm::state
