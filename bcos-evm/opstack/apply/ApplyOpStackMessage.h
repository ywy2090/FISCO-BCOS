/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief OpStack chain entry: deposit vs normal tx orchestration around stateTransitionExecute.
 * @file ApplyOpStackMessage.h
 *
 * Parallel to eth/apply/ApplyEthMessage.h. OpStack adds:
 *   - L1 / operator fee settlement (OpStackNormalTxFeeCoordinator, settleDeposit)
 *   - OpStackChainCallTargetAdapter (L1Block, GasPriceOracle predeploys)
 *   - Deposit-tx mint + receipt meta (Canyon deposit receipt version)
 *
 * Pipeline (applyOpStackMessage):
 *   wire ctx → bind hooks → lifecycleCheckEntryRules
 *   → [deposit | normal] gas pool → checkpoint → stateTransitionExecute → settlement
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/GasSettlementTypes.h"
#include "bcos-evm/eth/kernel/EVMCResult.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/StateView.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-evm/opstack/settlement/OpStackFeeSettlement.h"
#include "bcos-evm/opstack/types/OpStackDepositTx.h"
#include "bcos-evm/opstack/types/OpStackReceiptMeta.h"
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace bcos::evm
{

/// Inputs for one OpStack transaction apply (L1-derived deposit or L2 user tx).
struct OpStackMessageRequest
{
    // ── Execution environment ─────────────────────────────────────────────
    state::StateView const* stateView{nullptr};
    evmc::VM* vm{nullptr};
    bcos::crypto::Hash const* hashImpl{nullptr};

    // ── Block context ─────────────────────────────────────────────────────
    state::BlockInfo blockInfo{};
    state::BlockHashes blockHashes{};
    bcos::evm::RevisionConfig revisionConfig{};
    /// Fork activation times for L1/operator fee selectors and predeploy dispatch.
    OpStackForkSchedule forkSchedule = makeIsthmusPlusForkSchedule();

    // ── Transaction envelope ────────────────────────────────────────────────
    evmc_message message{};
    /// Sender account nonce claimed by the signed tx; checked against state in
    /// lifecycleCheckEntryRules.
    uint64_t nonce{0};
    /// For EVM trace logging only; not used in consensus validation.
    std::optional<bcos::h256> txHash;

    // ── Fee caps (EIP-1559 / EIP-4844) ──────────────────────────────────────
    bcos::u256 gasTipCap{0};
    bcos::u256 gasFeeCap{0};
    bcos::u256 blobGasFeeCap{0};
    std::vector<bcos::h256> blobVersionedHashes;

    // ── Typed tx extensions (EIP-2930 / 4844 / 7702) ──────────────────────
    /// EIP-2718 type byte; OpStackDeposit selects the deposit settlement path.
    uint8_t web3TypedTxKind{0};
    /// Owned by the request so the list outlives execution (Fisco request uses the same
    /// model); downstream consumers observe via .get(). A raw pointer here dangled when
    /// the builder's resolver local was the sole owner.
    std::shared_ptr<const Eip2930AccessList> accessList{};
    /// True when the typed tx envelope carries an EIP-7702 authorization list field.
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;

    // ── Deposit tx (L1 → L2) ───────────────────────────────────────────────
    /// Bedrock deposit payload; mint + nonce rules; exempt from L1/operator fee buyGas.
    std::optional<OpStackDepositTx> depositTx;

    // ── Execution mode flags ────────────────────────────────────────────────
    /// eth_call / read-only simulation (executor sets from m_call).
    bool call{false};
    /// Skip stateNonce == nonce check (eth_call, synthetic tests). Default false for block txs.
    bool skipNonceChecks{false};
    /// Skip fee-cap / blob / gas-limit entry rules and relax value balance in floor precheck.
    bool skipTransactionChecks{false};
    /// With call + zero fee caps: refundGas skips all balance mutations (zero-fee eth_call).
    bool noBaseFee{false};

    // ── Op Stack fee inputs ─────────────────────────────────────────────────
    /// Optional pre-seeded EIP-7623 floor (EEST); when 0, opStackFloorGasPrecheck computes into
    /// sidecar.
    uint64_t floorDataGas{0};
    /// Serialized-tx DA stats for Fjord L1 fee; empty RollupCostData for deposits (op-geth parity).
    std::optional<RollupCostData> rollupCostData;

    // ── Settlement hooks ────────────────────────────────────────────────────
    /// Block-level gas pool acquire before execute; unset in unconstrained tests.
    std::function<bool(uint64_t)> gasPoolSubGasHook;
    /// Return unused gas limit + report consumed gas after settlement.
    std::function<void(uint64_t gasRemaining, uint64_t gasUsed)> gasPoolReturnGasHook;
    /// Fee debit/refund ledger; l1/operator funcs wired inside applyOpStackMessage from L1Block
    /// state.
    OpStackFeeSettlement opTxExecutor{};
};

struct OpStackMessageResult
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    int64_t gasUsed{0};
    OpStackReceiptMeta receiptMeta;
    IntrinsicGasAccounting gasAccounting{};
    /// True when top-level EVM returned a non-REVERT vm error that is still included on-chain
    /// (ADR-015).
    bool topLevelIncludedTxVmError{false};
};

// ── Chain entry ───────────────────────────────────────────────────────────────
task::Task<OpStackMessageResult> applyOpStackMessage(OpStackMessageRequest input);

/// True for L1 deposit transactions (typed tx kind or explicit depositTx payload).
inline bool isDepositTx(OpStackMessageRequest const& input) noexcept
{
    return input.web3TypedTxKind == toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit) ||
           input.depositTx.has_value();
}
}  // namespace bcos::evm
