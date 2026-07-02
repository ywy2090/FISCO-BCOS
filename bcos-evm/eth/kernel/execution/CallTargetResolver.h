/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Call-target classification: address resolution seam + precompile / EVM dispatch kind.
 * @file CallTargetResolver.h
 *
 * Pipeline position (EvmCallFrame::runFrameSteps):
 *   routeFrameMessage → classifyCallTarget → runCallTargetFastPath / loadFrameBytecode
 *
 * Merges address work (via FrameRouting) with target kind selection (ADR-024). PrecompileRouter
 * consumes a pre-classified CallTargetDescriptor; it does not re-classify.
 *
 * CallTargetKind values (see CallTargetTypes.h):
 *   EvmContract        — normal VM path (incl. EIP-7702 designator accounts)
 *   BuiltinPrecompile  — eth precompile table (PrecompileActive)
 *   ChainPrecompile    — FISCO / OpStack adapter (ChainCallTargetPort)
 *   EmptyAccount       — empty code, not a precompile
 *   PolicyRejected     — e.g. DELEGATECALL to precompile when disallowed by extension
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/core/CallTargetTypes.h"
#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/kernel/FrameScope.h"
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm::execution
{

/// Classify one frame's call target after routing (geth: run() precompile vs contract dispatch).
///
/// Internally calls routeFrameMessage; executionAddress is the classification primary key.
/// @param callTargetPort Chain extension (FISCO [PRECOMPILED], OpStack predeploys); may be null.
/// @param extension      Host hooks; DELEGATECALL-to-precompile policy via
/// allowDelegateCallToPrecompile.
CallTargetDescriptor classifyCallTarget(state::State& state,
    bcos::evm::RevisionConfig const& revision, evmc_message msg, FrameScope scope,
    ChainCallTargetPort* callTargetPort, state::EvmHostHooks* extension);

/// Emit all addresses warmed at transaction entry (EIP-2929).
///
/// Builtin precompiles (PrecompileActive) plus chain static targets from callTargetPort.
/// Used by prepareState; must stay aligned with WarmPolicy assigned in classifyCallTarget.
void enumerateTxEntryWarmTargets(bcos::evm::RevisionConfig const& cfg,
    ChainCallTargetPort const* callTargetPort,
    std::function<void(evmc_address const&)> const& consume);

}  // namespace bcos::evm::execution
