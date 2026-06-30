/*
 * @brief Chain entry applyOpStackMessage (geth ApplyMessage; ADR-030 Tier C).
 * @file ApplyOpStackMessage.h
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/InnerExecute.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackFeeSettlement.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos-evm/opstack/OpStackReceiptMeta.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include <bcos-framework/executor/OpStackTxType.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <functional>
#include <optional>
#include <vector>

namespace bcos::evm
{
struct OpStackExecutionRequest
{
    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};
    evmc_message message{};
    uint64_t nonce{0};
    bcos::u256 gasTipCap{0};
    bcos::u256 gasFeeCap{0};
    bcos::u256 blobGasFeeCap{0};
    std::vector<bcos::h256> blobVersionedHashes;
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm_standard::RevisionConfig revisionConfig{};
    state::TransactionProperties txProps{};
    const Eip2930AccessList* accessList{nullptr};
    uint8_t web3TypedTxKind{0};
    std::optional<OpStackDepositTx> depositTx;
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    bool call{false};
    bool skipNonceChecks{false};
    bool skipTransactionChecks{false};
    bool noBaseFee{false};
    uint64_t floorDataGas{0};
    std::optional<RollupCostData> rollupCostData;
    OpStackForkSchedule forkSchedule = makeIsthmusPlusForkSchedule();
    std::function<bool(uint64_t)> gasPoolSubGasHook;
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> gasPoolReturnGasHook;
    OpStackFeeSettlement opTxExecutor{};
    std::optional<bcos::h256> txHash;
};

struct OpStackExecutionResult
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<LogEntry> logs;
    int64_t gasUsed{0};
    OpStackReceiptMeta receiptMeta;
};

// ── Chain entry: geth ApplyMessage (ADR-030 Tier C canonical) ─────────────────
task::Task<OpStackExecutionResult> applyOpStackMessage(OpStackExecutionRequest input);

inline bool isDepositTx(OpStackExecutionRequest const& input) noexcept
{
    return input.web3TypedTxKind == bcos::executor::DEPOSIT_TX_TYPE || input.depositTx.has_value();
}
}  // namespace bcos::evm
