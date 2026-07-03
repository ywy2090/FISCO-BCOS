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
 * @brief Unified EVM call/create frame entry (top-level tx and nested host calls).
 * @file EvmCallFrame.h
 *
 * Single orchestration point for one evmc_message frame: target routing, value transfer,
 * evmone execution, and journal commit/revert. Invoked from innerExecute (TopLevel)
 * and EthHost::call (Nested).
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/kernel/FrameScope.h"
#include <evmc/evmc.hpp>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class State;
class EthHost;
struct EvmHostHooks;
}  // namespace bcos::evm::state

namespace bcos::evm::execution
{

/// Per-frame execution bundle shared across nested calls for one transaction.
struct CallFrameContext
{
    state::State& state;
    evmc::VM& vm;
    bcos::evm::RevisionConfig const& revisionConfig;
    /// Chain-specific host hooks (FISCO storage status, nonce policy, etc.).
    state::EvmHostHooks* extension{nullptr};
    /// Optional chain precompile / policy dispatch (FISCO, OpStack adapters).
    ChainCallTargetPort* callTargetPort{nullptr};
    evmc_address txOrigin{};
    /// Current execution address; updated on nested CALL success (delegate routing).
    evmc_address& executionAddress;

    CallFrameContext(state::State& state_, evmc::VM& vm_,
        bcos::evm::RevisionConfig const& revisionConfig_, state::EvmHostHooks* extension_,
        evmc_address txOrigin_, evmc_address& executionAddress_,
        ChainCallTargetPort* callTargetPort_ = nullptr) noexcept
      : state(state_),
        vm(vm_),
        revisionConfig(revisionConfig_),
        extension(extension_),
        callTargetPort(callTargetPort_),
        txOrigin(txOrigin_),
        executionAddress(executionAddress_)
    {}
};

/// Outcome of runCallFrame; envelopeComplete skips post-execute host bookkeeping in the runner.
struct FrameResult
{
    evmc::Result result{evmc_result{}};
    int64_t gasRefund{0};
    bool envelopeComplete{false};
};

/// Run one CALL/CREATE/DELEGATECALL frame. scope selects TopLevel vs Nested semantics.
FrameResult runCallFrame(
    CallFrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host);

}  // namespace bcos::evm::execution
