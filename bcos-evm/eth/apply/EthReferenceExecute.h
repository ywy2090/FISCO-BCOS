#pragma once

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/EthExecutionArtifacts.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-task/Task.h"
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
struct EthReferenceRequest
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

struct EthReferenceResult
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    EthExecutionArtifacts executionContext;
    bool topLevelIncludedTxVmError{false};
};

// ── Chain entry: geth ApplyMessage (ADR-030 dual-label) ─────────────────────
// Tier C canonical — document and prefer in new call sites: applyReferenceMessage
// Tier E stable ABI — retain for existing links; no [[deprecated]] yet: ethReferenceExecute
task::Task<EthReferenceResult> ethReferenceExecute(EthReferenceRequest input);

// geth: ApplyMessage — ADR-030 Tier C canonical (forwards to ethReferenceExecute)
[[nodiscard]] inline task::Task<EthReferenceResult> applyReferenceMessage(EthReferenceRequest input)
{
    return ethReferenceExecute(std::move(input));
}

}  // namespace bcos::evm
