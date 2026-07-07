/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Shared gas settlement DTOs (intrinsic breakdown, fee plans, pipeline bookkeeping).
 * @file GasSettlementTypes.h
 *
 * Pure data types only — no State access. Formulas live in TxIntrinsicGas.h / TxFeeSettlement.h;
 * state-transition wiring in kernel/state-transition/; Eth ledger in eth/settlement/.
 *
 * Module map:
 *   ProtocolGas.h          — Yellow Paper numeric literals
 *   GasSettlementTypes.h   — this file (DTOs)
 *   TxIntrinsicGas.h       — pre-EVM intrinsic debit breakdown
 *   TopLevelGasSettlement.h — post-EVM gasUsed (3529 refund cap, 7623 floor)
 *   TxFeeSettlement.h      — EIP-1559 wei projection (buyGas / refundGas plan)
 *   PostExecuteGasMetering.h — Eth reference path gasUsed metering
 *   TxGasUsedGate.h        — TE/EEST fork gate (not used on applyEthMessage path)
 *   TxGasLifecycle.h       — chronological index of the above
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/gas/ProtocolGas.h"
#include "bcos-utilities/Common.h"
#include <algorithm>
#include <cstdint>

namespace bcos::evm
{

/// Gas bookkeeping across stateTransitionExecute (lives on StateTransitionContext).
/// Separates intrinsic debit from EVM opcode consumption for diagnostics and characterization
/// tests.
struct IntrinsicGasAccounting
{
    /// message.gas immediately before deductIntrinsicGas (after preCheck hooks).
    int64_t gasBeforeIntrinsicDebit{0};

    /// debitOutcome.debitAmount on successful deductIntrinsicGas; 0 otherwise.
    int64_t intrinsicGasDebited{0};

    /// message.gas right after successful intrinsic debit.
    int64_t gasAfterIntrinsicDebit{0};

    /// message.gas immediately before onInvokeInnerExecute (after onTuneInnerExecuteInput).
    /// -1 when pipeline never reached EVM entry.
    int64_t gasAtEvmEntry{-1};

    bool intrinsicDebitAttempted{false};  ///< deductIntrinsicGas was entered
    bool intrinsicDebitSucceeded{false};  ///< debit completed without IntrinsicRejected
    bool reachedEvmEntry{false};          ///< onInvokeInnerExecute was reached
};

namespace gas
{

/// Breakdown of gas debited before EVM entry (geth IntrinsicGas + Charge).
/// Used by deductIntrinsicGas, txpool prechecks, and gasLimitMinimum validation.
struct TxIntrinsicGas
{
    int64_t txBase = TX_BASE_GAS;
    int64_t normalCalldata = 0;   ///< Legacy calldata cost (EIP-7623 "normal" component)
    int64_t accessListCost = 0;   ///< EIP-2930 warm-address/key charges
    int64_t createIntrinsic = 0;  ///< CREATE/CREATE2 base + initcode words
    int64_t floorReserve =
        0;  ///< Eip7623Components::floorCost (full floor term, incl. in gasLimitMinimum)

    /// txBase + accessListCost — portion independent of calldata bytes and CREATE initcode.
    int64_t fixedCost() const { return txBase + accessListCost; }

    /// Total intrinsic debited from message.gas before innerExecute.
    int64_t preExecutionDebit() const { return fixedCost() + normalCalldata + createIntrinsic; }

    /// EIP-7623: max(21000 + floor, intrinsic) where intrinsic includes access list + CREATE.
    int64_t gasLimitMinimum() const
    {
        int64_t const intrinsic = preExecutionDebit();
        int64_t const floorTotal = txBase + floorReserve;
        return std::max(intrinsic, floorTotal);
    }

    /// EIP-7623 + EIP-7702: auth intrinsic is part of the intrinsic side of the minimum, not
    /// additive on top of the floor comparison.
    int64_t gasLimitMinimumWithAuth(int64_t authCost) const
    {
        int64_t const intrinsic = preExecutionDebit() + authCost;
        int64_t const floorTotal = txBase + floorReserve;
        return std::max(intrinsic, floorTotal);
    }
};

/// Post-EVM inputs for top-level gasUsed settlement (EIP-3529 refund + EIP-7623 floor).
/// Populated by captureSettlementSnapshot when IntrinsicGasMode::FloorDataGas is active.
struct TxGasSettlementSnapshot
{
    /// Set by captureSettlementSnapshot; gates EIP-7623 peak settlement on the Eth reference path.
    /// Replaces the legacy snapshot.gasLimit>0 sentinel (ADR-005: no isWeb3 in eth layer).
    bool eip7623SnapshotActive{false};
    Eip7623Components calldata{};  ///< Pre-EVM calldata cost components (not raw tx bytes)
    int64_t evmGasRefund =
        0;  ///< state.get_refund() at EVM exit (authoritative over evmc gas_refund)
};

/// Alias kept for settlement-layer parameter names; same type as TxGasSettlementSnapshot.
using TxGasSettlementContext = TxGasSettlementSnapshot;

/// Per-tx fee market inputs for planPreExecution / planPostExecution (ADR-026).
/// Mapped from pipeline context by FeeInputsMapping.h; EEST/GST may construct manually.
struct FeeInputs
{
    bcos::evm::RevisionConfig const& revision;
    bcos::u256 baseFee;  ///< Block base fee (wei); from blockInfo, not recomputed here
    int64_t gasLimit{0};
    bcos::u256 gasPrice;             ///< Legacy gasPrice field (pre-1559 or fallback)
    bcos::u256 gasTipCap;            ///< maxPriorityFeePerGas
    bcos::u256 gasFeeCap;            ///< maxFeePerGas
    uint8_t web3TypedTxKind{0};      ///< EIP-2718 type byte (EIP1559 / EIP7702 → 1559 caps)
    bool hasExplicitFeeCaps{false};  ///< Typed tx or explicit cap fields from TE builder
};

/// Projected wei amounts for buyGas (pre) and refundGas (post). Orchestrator maps fields to State.
struct FeeSettlementPlan
{
    // --- planPreExecution (执行前填写的三个字段) ---

    /// 实际 gas 单价 (wei/gas)。
    bcos::u256 effectiveGasPrice;

    /// 余额充足性检查上限 (wei)，不是实际扣款。
    bcos::u256 maxBalanceDebit;

    /// buyGas 从 sender 实际预扣的 wei：gasLimit × effectiveGasPrice。
    bcos::u256 preDebitAmount;

    // --- planPostExecution (执行后填写；pre 阶段为零) ---

    /// 退还给 sender 的 wei：gasRemaining × effectiveGasPrice。
    bcos::u256 unusedRefund;

    /// 付给 coinbase 的小费 wei：gasUsed × max(0, effectiveGasPrice - baseFee)。
    bcos::u256 coinbaseTip;

    /// base fee 分量 wei：gasUsed × baseFee。
    bcos::u256 baseFeeAmount;

    /// sender 执行 gas 净支出 wei：gasUsed × effectiveGasPrice。
    bcos::u256 senderNetDebit;
};

/// Post-execution gasUsed / gasRemaining in gas units (before wei refund in refundGas).
/// Produced by meterPostExecuteGas; consumed by EthFeeSettlement::refundGas.
struct PostExecuteGasResult
{
    int64_t gasUsed{0};
    uint64_t gasRemaining{0};
};

}  // namespace gas
}  // namespace bcos::evm

namespace bcos::executor_v1::gas
{
using bcos::evm::gas::FeeInputs;
using bcos::evm::gas::FeeSettlementPlan;
using bcos::evm::gas::TxIntrinsicGas;
}  // namespace bcos::executor_v1::gas
