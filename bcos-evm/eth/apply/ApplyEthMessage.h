/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Chain entry applyEthMessage.
 * @file ApplyEthMessage.h
 */

#pragma once

#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/kernel/state-transition/StateTransitionContext.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-task/Task.h"
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{

/// Pure-ethereum orchestration: innerExecute + EthEvmHostHooks (reference path only).
struct EthMessageRequest
{
    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};

    evmc_message message{};
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm::RevisionConfig revisionConfig{};
    bcos::u256 gasPrice{0};
    bcos::u256 gasTipCap{0};
    bcos::u256 gasFeeCap{0};
    bcos::u256 blobGasFeeCap{0};
    std::vector<bcos::h256> blobVersionedHashes;
    uint8_t web3TypedTxKind{0};
    bool hasExplicitFeeCaps{false};
    /// Owned by the request so the list outlives execution (Fisco request uses the same
    /// model); downstream consumers observe via .get(). A raw pointer here dangled when
    /// the builder's resolver local was the sole owner.
    std::shared_ptr<const Eip2930AccessList> accessList{};
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    uint64_t txNonce{0};
    std::optional<bcos::h256> txHash;
    bool isCall{false};
    bcos::u256 txValue{0};
};

struct EthMessageResult
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    evmc_message message{};
    bcos::evm::RevisionConfig revisionConfig{};
    std::vector<protocol::LogEntry> receiptLogs;
    gas::TxGasSettlementContext gasSettlementSnapshot{};
    bool topLevelIncludedTxVmError{false};
    StateTransitionExitKind exitKind{StateTransitionExitKind::None};
    int64_t gasUsed{0};
    bcos::u256 effectiveGasPrice{0};
    std::string gasPriceStr;
};

// ── Chain entry ───────────────────────────────────────────────────────────────
task::Task<EthMessageResult> applyEthMessage(EthMessageRequest input);

}  // namespace bcos::evm
