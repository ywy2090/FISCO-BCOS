/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Mutable execution context for stateTransitionExecute.
 * @file StateTransitionContext.h
 *
 * Holds the evmc_message, state view, gas accounting, and pipeline outputs
 * (evmcResult, logs, stateDiff). Constructed by each chain's apply*Message,
 * then passed through hooks and errorPolicy. Non-copyable: one context per tx.
 */

#pragma once

#include "bcos-evm/eth/core/EvmHostHooks.h"
#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/kernel/state-transition/DeductIntrinsicGas.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.hpp>
#include <stdexcept>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

struct ChainCallTargetPort;
struct InnerExecuteInput;

/// How stateTransitionExecute terminated (for logging and downstream settlement).
enum class StateTransitionExitKind
{
    None,
    RulesRejected,      ///< onPreCheckRules set earlyExit (nonce, initcode, etc.)
    GasAffordRejected,  ///< buyGas / balance precheck failed
    IntrinsicRejected,  ///< deductIntrinsicGas failed (gas limit floor or calldata OOG)
    Completed,          ///< EVM ran; onFinalizeGasUsed applied
    ExceptionHandled    ///< C++ exception mapped by errorPolicy.onException
};

/// Chain-agnostic inputs wired before stateTransitionExecute (via *ExecutionBundle).
struct StateTransitionInputs
{
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    Eip2930AccessList const* accessList{nullptr};
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    uint8_t web3TypedTxKind{0};
    std::vector<bcos::h256> blobVersionedHashes;
};

class StateTransitionContext
{
public:
    StateTransitionContext(state::StateView const& stateView, evmc_message inputMessage,
        bcos::evm::RevisionConfig inputRevisionConfig, bcos::u256 inputGasPrice)
      : message(inputMessage),
        originalGasLimit(inputMessage.gas),
        state(stateView),
        gasPrice(inputGasPrice),
        revisionConfig(inputRevisionConfig)
    {}

    StateTransitionContext(StateTransitionContext const&) = delete;
    StateTransitionContext& operator=(StateTransitionContext const&) = delete;
    StateTransitionContext(StateTransitionContext&&) = delete;
    StateTransitionContext& operator=(StateTransitionContext&&) = delete;

    /// Chain Bundle injection: vm (required), host hooks, call-target port (optional).
    void wireExecutionEnvironment(
        evmc::VM* vm, state::EvmHostHooks* extension, ChainCallTargetPort* callTargetPort)
    {
        if (vm == nullptr)
        {
            throw std::invalid_argument(
                "StateTransitionContext::wireExecutionEnvironment requires vm");
        }
        inputs.vm = vm;
        this->extension = extension;
        this->callTargetPort = callTargetPort;
    }

    InnerExecuteInput toInnerExecuteInput() const;

    StateTransitionInputs inputs;
    evmc_message message{};
    int64_t originalGasLimit{0};  ///< Tx gas limit before any pipeline debit (settlement baseline).
    state::State state;
    bcos::u256 gasPrice{0};
    state::EvmHostHooks* extension{nullptr};
    ChainCallTargetPort* callTargetPort{nullptr};
    bcos::evm::RevisionConfig revisionConfig{};
    gas::TxGasSettlementSnapshot snapshot{};  ///< EIP-7623 calldata components for refund.
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    int64_t evmGasRefund{0};
    EVMCResult evmcResult{evmc_result{}};
    bool earlyExit{false};  ///< Set by precheck hooks to skip EVM without throwing.
    StateTransitionExitKind exitKind{StateTransitionExitKind::None};
    IntrinsicGasMode intrinsicGasMode{IntrinsicGasMode::Skip};
    IntrinsicGasAccounting gasAccounting{};
    /// Eth-only: set by EthStateTransitionErrorPolicy when top-level vmerr is included in block.
    bool topLevelIncludedTxVmError{false};
};

}  // namespace bcos::evm
