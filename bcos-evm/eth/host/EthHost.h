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
 * @brief evmc::Host adapter over eth::state::State for evmone execution.
 * @file EthHost.h
 *
 * EthHost is the state-facing half of evm.Call: evmone invokes these callbacks
 * for account/storage/code access, nested calls, logs, and Berlin+ access lists.
 * It does not own transaction lifecycle — that stays in state-transition /
 * `runCallFrame`; EthHost only bridges evmc host interface → `State` journal.
 *
 * Extension seams (ADR-005):
 *   - `EvmHostHooks* m_extension` — SSTORE refund/status, SELFDESTRUCT gate
 *   - `ChainCallTargetPort* m_callTargetPort` — chain precompile routing
 *     (wired through `CallFrameContext`, consumed inside `runCallFrame`)
 *
 * Per-transaction side state kept here (not in `State`):
 *   - `m_logs` — LOG0..4 emissions, drained by `take_logs()` after execution
 *   - `m_createdInTx` — CREATE addresses for EIP-6780 SELFDESTRUCT semantics
 *   - `m_storageOriginalValues` — first SSTORE touch original slot (EIP-2200)
 *   - `m_executionAddress` — current frame execution address for nested CREATE
 *
 * Wired once per transaction in `StateTransitionContext::wireExecutionEnvironment`
 * and passed into `runCallFrame` as the host for both top-level and nested depth.
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateKeyHash.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class State;
struct EvmHostHooks;
/// evmc::Host implementation backed by `State` journal + revision-aware EIP helpers.
///
/// Lifetime: one instance per transaction, shared across all nested call depths.
/// Wired in `StateTransitionContext::wireExecutionEnvironment`; evmone holds a
/// pointer for the duration of `runCallFrame`.
class EthHost : public evmc::Host
{
public:
    using address = evmc::address;
    using bytes32 = evmc::bytes32;
    using uint256be = evmc::uint256be;
    using Result = evmc::Result;

    /// @param state            Mutable execution journal (balance/code/storage overlay).
    /// @param txContext        BLOCKHASH / ORIGIN / GASPRICE context for opcodes.
    /// @param revisionConfig   Fork flags (EIP-6780, EIP-2929, EIP-1153, …).
    /// @param vm               evmone VM instance used for nested `call()`.
    /// @param blockHashes      Optional BLOCKHASH lookup; empty functor returns zero hash.
    /// @param extension        Chain SSTORE/SELFDESTRUCT policy; nullptr → ETH defaults.
    /// @param callTargetPort   Chain precompile routing; consumed inside `runCallFrame`.
    EthHost(State& state, evmc_tx_context txContext, bcos::evm::RevisionConfig revisionConfig,
        evmc::VM& vm, BlockHashes blockHashes, EvmHostHooks* extension = nullptr,
        ChainCallTargetPort* callTargetPort = nullptr);

    // --- Account / storage / code (evmc host interface → State journal) ---

    bool account_exists(const address& addr) const noexcept final;
    bytes32 get_storage(const address& addr, const bytes32& key) const noexcept final;
    /// EIP-2200/3529 storage status + refund; original slot cached in `m_storageOriginalValues`.
    evmc_storage_status set_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept final;
    uint256be get_balance(const address& addr) const noexcept final;
    size_t get_code_size(const address& addr) const noexcept final;
    bytes32 get_code_hash(const address& addr) const noexcept final;
    size_t copy_code(const address& addr, size_t code_offset, uint8_t* buffer_data,
        size_t buffer_size) const noexcept final;

    /// EIP-6780: pre-existing contracts transfer balance only; tx-created contracts
    /// are marked via `mark_self_destructed`. Pre-6780: wipes code + storage.
    /// Returns `true` when account is considered destroyed (evmone refund semantics).
    bool selfdestruct(const address& addr, const address& beneficiary) noexcept final;

    /// Nested CALL/CREATE entry — builds `CallFrameContext` and delegates to `runCallFrame`.
    /// Saves/restores `m_executionAddress` around the nested frame.
    Result call(const evmc_message& msg) noexcept final;

    // --- Block / transaction context ---

    evmc_tx_context get_tx_context() const noexcept final;
    bytes32 get_block_hash(int64_t number) const noexcept final;

    // --- Logs (accumulated per tx, drained by `take_logs`) ---

    void emit_log(const address& addr, const uint8_t* data, size_t data_size,
        const bytes32 topics[], size_t num_topics) noexcept final;

    // --- EIP-2929 warm/cold access (no-op gas-wise when revision disables eip2929) ---

    evmc_access_status access_account(const address& addr) noexcept final;
    evmc_access_status access_storage(const address& addr, const bytes32& key) noexcept final;

    // --- EIP-1153 transient storage (Cancun+) ---

    bytes32 get_transient_storage(const address& addr, const bytes32& key) const noexcept final;
    void set_transient_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept final;

    // --- Frame-local helpers (not part of evmc::Host) ---
    /// Current frame address for CREATE/CREATE2 side effects (updated by `EvmCallFrame`).
    void set_execution_address(const evmc_address& address) noexcept
    {
        m_executionAddress = address;
    }
    evmc_address& execution_address_ref() noexcept { return m_executionAddress; }

    /// Record a contract address created in this transaction (EIP-6780 SELFDESTRUCT gate).
    /// Called from `EvmCallFrame` on successful CREATE; zero address is ignored.
    void markCreatedInTx(evmc_address const& addr) noexcept;
    [[nodiscard]] bool wasCreatedInTx(evmc_address const& addr) const noexcept;

    /// Move accumulated LOG entries out; host log buffer is cleared after call.
    std::vector<LogEntry> take_logs();

private:
    /// Pre-EIP-6780 path: zero code hash + clear all storage slots on SELFDESTRUCT.
    void destroyContractState(evmc_address const& addr) noexcept;

    /// Execution journal (shared at all nested call depths).
    State& m_state;
    /// Tx/block metadata for opcode context (ORIGIN, GASPRICE, …).
    evmc_tx_context m_txContext{};
    /// Active fork profile for this block (EIP-6780, EIP-2929, …).
    bcos::evm::RevisionConfig m_revisionConfig{};
    /// evmone instance used for nested `call()`.
    evmc::VM& m_vm;
    /// BLOCKHASH(number) lookup; empty functor returns zero hash.
    BlockHashes m_blockHashes;
    /// Chain SSTORE/SELFDESTRUCT hooks (ADR-005); nullptr → ETH defaults.
    EvmHostHooks* m_extension{nullptr};
    /// Chain precompile routing port (ADR-005); consumed inside `runCallFrame`.
    ChainCallTargetPort* m_callTargetPort{nullptr};
    /// Original slot value at first SSTORE in this tx (per addr,key) for EIP-2200 status.
    std::unordered_map<std::pair<address, bytes32>, bytes32, WarmStorageKeyHash,
        WarmStorageKeyEqual>
        m_storageOriginalValues;
    /// LOG0..4 buffer; moved out via `take_logs()` after execution.
    std::vector<LogEntry> m_logs;
    /// Active frame address for nested CREATE/CREATE2 side effects.
    evmc_address m_executionAddress{};
    /// Addresses created via CREATE/CREATE2 in this tx; drives EIP-6780 selfdestruct.
    std::unordered_set<evmc_address, AddressHash, AddressEqual> m_createdInTx;
};
}  // namespace bcos::evm::state
