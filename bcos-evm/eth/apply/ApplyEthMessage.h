/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Chain entry applyEthMessage.
 * @file ApplyEthMessage.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-task/Task.h"
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <functional>
#include <optional>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

/// Pure-ethereum orchestration: innerExecute + EthVmHostPolicy (reference path only).
struct EthMessageRequest
{
    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};

    evmc_message message{};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    bcos::u256 gasPrice{0};
    bcos::u256 gasTipCap{0};
    bcos::u256 gasFeeCap{0};
    uint8_t web3TypedTxKind{0};
    bool hasExplicitFeeCaps{false};
    const Eip2930AccessList* accessList{nullptr};
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    uint64_t txNonce{0};
    std::optional<bcos::h256> txHash;
};

struct EthMessageResult
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    evmc_message message{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    std::vector<protocol::LogEntry> receiptLogs;
    gas::TxGasSettlementContext gasSettlementSnapshot{};
    bool topLevelIncludedTxVmError{false};
};

// ── Chain entry ───────────────────────────────────────────────────────────────
task::Task<EthMessageResult> applyEthMessage(EthMessageRequest input);

}  // namespace bcos::evm
