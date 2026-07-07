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
 * @brief EVM kernel entry between state-transition pipeline and frame execution (geth
 * innerExecute).
 * @file InnerExecute.h
 *
 * Canonical portable entry for one `evmc_message` execution pass. Eth, FISCO, and OpStack
 * converge here via `StateTransitionHooks::onInvokeInnerExecute` after precheck / intrinsic gas.
 *
 * Call chain (production):
 *   apply*Message → stateTransitionExecute → onInvokeInnerExecute → innerExecute()
 *   Nested depth: EthHost::call → innerExecute (message.depth > 0)
 *
 * Pipeline (see InnerExecute.cpp):
 *   prepareState (tx-entry warm, EIP-1153 clear) → EthHost + tx context → runCallFrame → finalize
 *
 * Frame orchestration: `EvmCallFrame` (`runCallFrame`, `runCallTargetFastPath`).
 *
 * ADR-030: documented geth name `innerExecute`; C++ symbol matches geth `evm.go` innerExecute.
 */

#pragma once

#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <optional>
#include <vector>

namespace bcos::evm
{
struct ChainCallTargetPort;

namespace state
{
class State;
struct EvmHostHooks;
}  // namespace state

using LogEntry = state::LogEntry;

struct InnerExecuteInput
{
    /// Mutable execution journal; pipeline passes &StateTransitionContext::state.
    state::State* state{nullptr};
    evmc::VM* vm{nullptr};
    evmc_message message{};
    bcos::u256 gasPrice{0};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm::RevisionConfig revisionConfig{};
    const Eip2930AccessList* accessList{nullptr};
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    uint8_t web3TypedTxKind{0};
    state::EvmHostHooks* extension{nullptr};
    ChainCallTargetPort* callTargetPort{nullptr};
    /// EIP-4844 versioned blob hashes for BLOBHASH opcode (tx-scoped, all depths).
    std::vector<evmc_bytes32> blobHashes;
    /// When true, orchestration owns top-level sender nonce bump (kernel skips it).
    bool skipTopLevelSenderNonceBump{false};
    std::optional<bcos::h256> txHash;
};

struct InnerExecuteOutput
{
    evmc::Result result{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<LogEntry> logs;
    int64_t gasRefund{0};
};

/// Run one EVM execution pass for @p input.message (top-level tx when depth == 0).
///
/// Prerequisites: `input.state` and `input.vm` non-null; `message.gas` already reflects
/// post-intrinsic debit at top level. Mutates `*input.state` journal; returns logs, refund,
/// and `stateDiff` for the orchestration layer to adopt into receipt / fee settlement.
///
/// Top-level vs nested:
///   - depth 0: clears transient storage, runs `prepareState`, owns sender nonce bump policy
///   - depth > 0: skips tx-entry warm; journal finalize differs for envelope-complete vs VM path
InnerExecuteOutput innerExecute(InnerExecuteInput input);

}  // namespace bcos::evm
